import {
  Injectable,
  Logger,
  OnModuleInit,
  OnModuleDestroy,
  Inject,
  forwardRef,
  Optional,
} from '@nestjs/common';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { randomUUID } from 'crypto';

import { MqttService } from './mqtt.service';
import { MQTT_PUBLISH_TEMPLATES, MQTT_EVENTS } from './mqtt.constants';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { TreatmentVersion } from '../treatment/entities/treatment_version.entity';
import { GroupTreatmentAssignment } from '../group/entities/group_treatment_assignment.entity';
import { GroupNodeAssignment } from '../group/entities/group_node_assignment.entity';
import { TimerGroup, TimerGroupStatus } from '../group/entities/timer_group.entity';
import { GroupAssignedEvent, GroupUnassignedEvent } from '../group/events/group.events';
import { TreatmentVersionPublishedEvent } from '../treatment/events/treatment.events';
import { TreatmentVersionStatus } from '../treatment/entities/treatment_version.entity';
import { ControlSlotService } from '../control-slot/control-slot.service';
import { ControlSlotTargetType } from '../control-slot/entities/control_slot.entity';

@Injectable()
export class GroupScheduleSyncService implements OnModuleInit, OnModuleDestroy {
  private readonly logger = new Logger(GroupScheduleSyncService.name);
  private assignmentSeq = 1;
  private commandEnvelopeSeq = 1;
  private readonly onlineState = new Map<string, 'online' | 'offline'>();
  private readonly activeSyncs = new Map<string, Promise<void>>();
  private readonly syncDebounceTimers = new Map<string, NodeJS.Timeout>();

  constructor(
    @InjectRepository(DeviceStatus)
    private readonly deviceStatusRepo: Repository<DeviceStatus>,
    @InjectRepository(TreatmentVersion)
    private readonly treatmentVersionRepo: Repository<TreatmentVersion>,
    @InjectRepository(GroupTreatmentAssignment)
    private readonly groupTreatmentRepo: Repository<GroupTreatmentAssignment>,
    @InjectRepository(GroupNodeAssignment)
    private readonly groupNodeRepo: Repository<GroupNodeAssignment>,
    @InjectRepository(TimerGroup)
    private readonly timerGroupRepo: Repository<TimerGroup>,
    private readonly mqttService: MqttService,
    @Optional() private readonly eventEmitter?: EventEmitter2,
    @Optional()
    @Inject(forwardRef(() => ControlSlotService))
    private readonly controlSlotService?: ControlSlotService,
  ) {}

  onModuleDestroy(): void {
    for (const timer of this.syncDebounceTimers.values()) clearTimeout(timer);
    this.syncDebounceTimers.clear();
  }

  async onModuleInit(): Promise<void> {
    // Do not broadcast every active group on backend boot. A boot-time
    // broadcast was the main source of duplicate command bursts and could
    // starve the MQTT connection that carries gateway heartbeats. Each
    // gateway is reconciled from its own online transition instead.
    this.logger.log('Schedule sync boot broadcast disabled; waiting for scoped gateway online transitions.');
  }

  /**
   * Listen to group assignment changes and downlink to all online gateways.
   */
  @OnEvent('group.assigned')
  async handleGroupAssigned(event: GroupAssignedEvent): Promise<void> {
    this.logger.log(
      `Received group.assigned for Group #${event.groupId}, treatmentVersionId=${event.treatmentVersionId}, nodes=[${event.nodeIds.join(',')}]`,
    );
    if (!event.deviceId) {
      this.logger.warn(`Skipping unscoped group.assigned event for Group #${event.groupId}; no MQTT broadcast will be performed.`);
      return;
    }
    await this.syncGroup(event.groupId, event.deviceId);
  }

  /**
   * Listen to group unassigned changes and downlink unassign to all online gateways.
   */
  @OnEvent('group.unassigned')
  async handleGroupUnassigned(event: GroupUnassignedEvent): Promise<void> {
    this.logger.log(`Received group.unassigned for Group #${event.groupId}`);
    if (!event.deviceId) {
      this.logger.warn(`Skipping unscoped group.unassigned event for Group #${event.groupId}; no MQTT broadcast will be performed.`);
      return;
    }
    const gateways = [{ device_id: event.deviceId }];
    for (const dev of gateways) {
      await this.unassignGroupOnGateway(dev.device_id, event.groupId);
    }
  }

  /** Re-push schedules whenever a treatment version is published. */
  @OnEvent('treatment.version.published')
  async handleTreatmentVersionPublished(
    event: TreatmentVersionPublishedEvent,
  ): Promise<void> {
    this.logger.log(
      `Received treatment.version.published for Treatment #${event.treatmentId} v${event.versionNum} ` +
        `(${event.sprayDayS}s/${event.cooldownDayS}s)`,
    );
    const assignments = await this.groupTreatmentRepo.find({
      where: {
        treatment_version_id: event.versionId,
        active: true,
      },
    });
    if (assignments.length === 0) {
      this.logger.log(
        `TreatmentVersion #${event.versionId} has no active group assignment; nothing to downlink.`,
      );
      return;
    }
    for (const assignment of assignments) {
      for (const gateway of await this.getRelevantGateways()) {
        await this.syncGroup(assignment.group_id, gateway.device_id);
      }
    }
  }

  /**
   * When an ESP32 gateway reconnects or reports status online, push all active group schedules to it.
   */
  @OnEvent(MQTT_EVENTS.DEVICE_STATUS)
  async handleDeviceStatus(data: { deviceId: string; payload: any }): Promise<void> {
    const status = data.payload?.status;
    if (status !== 'online' && status !== 'offline') return;
    const previous = this.onlineState.get(data.deviceId);
    this.onlineState.set(data.deviceId, status);
    if (status !== 'online' || previous === 'online') return;

    // The first online event is already a reconnect boundary and should not
    // wait behind a timer. Subsequent offline -> online transitions are
    // debounced to coalesce MQTT reconnect/status bursts.
    if (previous === undefined) {
      const running = this.activeSyncs.get(data.deviceId);
      if (running) return running;
      const sync = this.syncAllActiveGroups(data.deviceId);
      this.activeSyncs.set(data.deviceId, sync);
      try {
        await sync;
      } finally {
        if (this.activeSyncs.get(data.deviceId) === sync) this.activeSyncs.delete(data.deviceId);
      }
      return;
    }

    const oldTimer = this.syncDebounceTimers.get(data.deviceId);
    if (oldTimer) clearTimeout(oldTimer);
    await new Promise<void>((resolve) => {
      const timer = setTimeout(() => {
        this.syncDebounceTimers.delete(data.deviceId);
        const running = this.activeSyncs.get(data.deviceId);
        if (running) {
          void running.then(() => resolve(), () => resolve());
          return;
        }
        const sync = this.syncAllActiveGroups(data.deviceId);
        this.activeSyncs.set(data.deviceId, sync);
        void sync.then(() => resolve(), () => resolve()).finally(() => {
          if (this.activeSyncs.get(data.deviceId) === sync) this.activeSyncs.delete(data.deviceId);
        });
      }, 5000);
      timer.unref?.();
      this.syncDebounceTimers.set(data.deviceId, timer);
    });
  }

  /**
   * Push treatment schedule and node assignments for a single group to gateways.
   */
  async syncGroup(groupId: number, targetDeviceId?: string): Promise<void> {
    const treatmentAssignment = await this.groupTreatmentRepo.findOne({
      where: { group_id: groupId, active: true },
      order: { assigned_at: 'DESC' },
    });

    if (!treatmentAssignment) {
      this.logger.warn(`No active treatment assignment found for Group #${groupId}`);
      return;
    }

    const version = await this.treatmentVersionRepo.findOne({
      where: { id: treatmentAssignment.treatment_version_id },
    });

    if (!version) {
      this.logger.warn(
        `TreatmentVersion #${treatmentAssignment.treatment_version_id} not found for Group #${groupId}`,
      );
      return;
    }

    const nodeAssignments = await this.groupNodeRepo.find({
      where: { group_id: groupId, active: true },
    });

    const gateways = targetDeviceId
      ? [{ device_id: targetDeviceId }]
      : await this.getRelevantGateways();

    if (gateways.length === 0) {
      this.logger.warn(`No online gateways found to sync Group #${groupId}`);
      return;
    }

    for (const gw of gateways) {
      if (targetDeviceId && !(await this.isGroupAllowed(targetDeviceId, groupId))) {
        this.logger.log(`Skipping ACTIVE sync for Group #${groupId} on ${targetDeviceId}: group is not present in control_slots.`);
        return;
      }
      const versions = await this.nextVersions(gw.device_id);
      // 1. Publish treatment config downlink
      const treatmentTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_TREATMENT_CONFIG(gw.device_id);
      const treatmentPayload = {
        command_id: randomUUID(),
        version: versions.command,
        group_id: groupId,
        season_id: treatmentAssignment.season_id,
        treatment_version_id: version.id,
        treatment_version: version.version_num,
        treatment_status: 'PUBLISHED',
        schedule: {
          spray_day_s: version.spray_day_s,
          cooldown_day_s: version.cooldown_day_s,
          spray_night_s: version.spray_night_s,
          cooldown_night_s: version.cooldown_night_s,
        },
      };

      try {
        await this.mqttService.publish(treatmentTopic, treatmentPayload);
        this.emitPublished(gw.device_id, groupId, 'TREATMENT', treatmentPayload.command_id);
        this.logger.log(
          `Published treatment config to ${treatmentTopic} (Group #${groupId}, v${version.version_num}, ${version.spray_day_s}s/${version.cooldown_day_s}s)`,
        );
      } catch (err: any) {
        this.logger.error(`Failed to publish treatment config: ${err.message}`);
        throw err;
      }

      // 2. Publish group-level authorization (ACTIVE) to the gateway.
      const groupStateTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_GROUP_STATE(gw.device_id);
      const groupStatePayload = {
        command_id: randomUUID(),
        version: versions.command,
        group_id: groupId,
        active: true,
      };
      try {
        await this.mqttService.publish(groupStateTopic, groupStatePayload);
        this.emitPublished(gw.device_id, groupId, 'GROUP_STATE', groupStatePayload.command_id);
        this.logger.log(
          `Published ACTIVE group state to ${groupStateTopic} (Group #${groupId})`,
        );
      } catch (err: any) {
        this.logger.error(
          `Failed to publish group state to ${groupStateTopic} for Group #${groupId}: ${err.message}`,
        );
        throw err;
      }

      // 3. Publish node-to-group assignment for each node
      const assignmentTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_ASSIGNMENT_CONFIG(gw.device_id);
      for (const na of nodeAssignments) {
        const assignmentPayload = {
          command_id: randomUUID(),
          version: versions.assignment,
          node_id: na.node_id,
          group_id: groupId,
        };

        try {
          await this.mqttService.publish(assignmentTopic, assignmentPayload);
          this.emitPublished(gw.device_id, groupId, 'ASSIGNMENT', assignmentPayload.command_id);
          this.logger.log(
            `Published assignment to ${assignmentTopic} (Node #${na.node_id} -> Group #${groupId})`,
          );
        } catch (err: any) {
          this.logger.error(`Failed to publish assignment: ${err.message}`);
          throw err;
        }
      }
    }
  }

  /**
   * Sync all currently ACTIVE groups to gateways.
   */
  async syncAllActiveGroups(targetDeviceId?: string): Promise<void> {
    const activeGroups = await this.timerGroupRepo.find({
      where: { status: TimerGroupStatus.ACTIVE },
    });

    const groups = targetDeviceId && this.controlSlotService
      ? (await this.controlSlotService.getSlots(targetDeviceId))
        .filter((slot) => slot.target_type === ControlSlotTargetType.GROUP && slot.target_id !== null)
        .map((slot) => Number(slot.target_id))
        .filter((id, index, ids) => ids.indexOf(id) === index)
        .map((groupId) => activeGroups.find((group) => group.group_id === groupId))
        .filter((group): group is TimerGroup => Boolean(group))
      : activeGroups;

    for (const group of groups) {
      await this.syncGroup(group.group_id, targetDeviceId);
    }
  }

  private async isGroupAllowed(deviceId: string, groupId: number): Promise<boolean> {
    // Keep degraded/test containers functional when the optional control-slot
    // provider is not registered. Production wiring always provides it.
    if (!this.controlSlotService) return true;
    const slots = await this.controlSlotService.getSlots(deviceId);
    return slots.some(
      (slot) => slot.target_type === ControlSlotTargetType.GROUP && slot.target_id === groupId,
    );
  }

  /**
   * Reconcile a group that is no longer authorized by the gateway's control
   * slots. This is intentionally public because a report can contain a
   * previously active group for which there is no active treatment assignment;
   * `syncGroup()` cannot handle that case by itself.
   */
  async unassignGroupOnGateway(deviceId: string, groupId: number): Promise<void> {
    const groupStateTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_GROUP_STATE(deviceId);
    const versions = await this.nextVersions(deviceId);
    const groupStateCommandId = randomUUID();
    try {
      await this.mqttService.publish(groupStateTopic, {
        command_id: groupStateCommandId,
        version: versions.command,
        group_id: groupId,
        active: false,
      });
      this.emitPublished(deviceId, groupId, 'GROUP_STATE', groupStateCommandId);
      this.logger.log(`Published UNASSIGNED group state to ${groupStateTopic} (Group #${groupId})`);
    } catch (err: any) {
      this.logger.error(
        `Failed to publish group unassign to ${groupStateTopic} for Group #${groupId}: ${err.message}`,
      );
      throw err;
    }

    const nodeAssignments = await this.groupNodeRepo.find({
      where: { group_id: groupId },
    });

    const assignmentTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_ASSIGNMENT_CONFIG(deviceId);
    for (const na of nodeAssignments) {
      const assignmentVersion = (await this.nextVersions(deviceId)).assignment;
      const assignmentPayload = {
        command_id: randomUUID(),
        version: assignmentVersion,
        node_id: na.node_id,
        group_id: 0, // 0 = UNASSIGNED
      };
      try {
        await this.mqttService.publish(assignmentTopic, assignmentPayload);
        this.emitPublished(deviceId, groupId, 'ASSIGNMENT', assignmentPayload.command_id);
      } catch (err: any) {
        this.logger.error(`Failed to publish unassign: ${err.message}`);
        throw err;
      }
    }
  }

  /**
   * Reconcile assignments observed on a gateway, including stale assignments
   * that no longer exist in the backend's active assignment table.
   */
  async reconcileNodeAssignments(
    deviceId: string,
    desiredAssignments: Array<{ nodeId: number; groupId: number }>,
    actualAssignments: Array<{ nodeId: number; groupId: number }>,
  ): Promise<void> {
    const desired = new Map(desiredAssignments.map((assignment) => [assignment.nodeId, assignment.groupId]));
    const actual = new Map(actualAssignments.map((assignment) => [assignment.nodeId, assignment.groupId]));
    const nodeIds = new Set([...desired.keys(), ...actual.keys()]);
    const topic = MQTT_PUBLISH_TEMPLATES.GATEWAY_ASSIGNMENT_CONFIG(deviceId);

    for (const nodeId of nodeIds) {
      const desiredGroupId = desired.get(nodeId) ?? 0;
      const actualGroupId = actual.get(nodeId) ?? 0;
      if (desiredGroupId === actualGroupId) continue;
      const versions = await this.nextVersions(deviceId);
      const payload = {
        command_id: randomUUID(),
        version: versions.assignment,
        node_id: nodeId,
        group_id: desiredGroupId,
      };
      await this.mqttService.publish(topic, payload);
      this.emitPublished(deviceId, desiredGroupId || actualGroupId, 'ASSIGNMENT', payload.command_id);
      this.logger.log(
        `Published assignment reconciliation deviceId=${deviceId} nodeId=${nodeId} ` +
        `actualGroupId=${actualGroupId} desiredGroupId=${desiredGroupId} ` +
        `assignmentVersion=${versions.assignment} commandId=${payload.command_id}`,
      );
    }
  }

  private async getRelevantGateways(): Promise<{ device_id: string }[]> {
    const list = await this.deviceStatusRepo.find({
      where: { status: 'online' },
    });
    if (list.length > 0) return list;
    // Offline gateways still receive command replay on reconnect; using the
    // registered device list avoids silently dropping an unassign.
    return this.deviceStatusRepo.find();
  }

  private nextCommandEnvelopeVersion(): number {
    // Keep within uint16_t, which is the firmware's envelope `version` type.
    this.commandEnvelopeSeq = (this.commandEnvelopeSeq % 65535) + 1;
    return this.commandEnvelopeSeq;
  }

  private async nextVersions(deviceId: string): Promise<{ command: number; assignment: number }> {
    // Keep older test doubles and degraded boot paths functional while the
    // persisted counters are introduced. Production repositories implement
    // both methods, so all normal traffic uses the per-device counters.
    if (
      typeof this.deviceStatusRepo.findOne !== 'function' ||
      typeof this.deviceStatusRepo.save !== 'function'
    ) {
      return {
        command: this.nextCommandEnvelopeVersion(),
        assignment: ++this.assignmentSeq,
      };
    }
    const status = await this.deviceStatusRepo.findOne({ where: { device_id: deviceId } });
    if (!status || typeof this.deviceStatusRepo.save !== 'function') {
      return { command: ++this.commandEnvelopeSeq, assignment: ++this.assignmentSeq };
    }
    const command = Number(status.command_envelope_version || 0) + 1;
    const assignment = Number(status.assignment_config_version || 0) + 1;
    status.command_envelope_version = String(command);
    status.assignment_config_version = String(assignment);
    await this.deviceStatusRepo.save(status);
    return { command, assignment };
  }

  private emitPublished(deviceId: string, groupId: number, commandType: string, commandId: string): void {
    this.eventEmitter?.emit('schedule.sync.command_published', {
      deviceId, groupId, commandType, commandId,
    });
    this.logger.debug(`Schedule command published deviceId=${deviceId} groupId=${groupId} commandType=${commandType} commandId=${commandId}`);
  }
}

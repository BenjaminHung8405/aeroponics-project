import {
  Injectable,
  Logger,
  OnModuleInit,
} from '@nestjs/common';
import { OnEvent } from '@nestjs/event-emitter';
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

@Injectable()
export class GroupScheduleSyncService implements OnModuleInit {
  private readonly logger = new Logger(GroupScheduleSyncService.name);
  private assignmentSeq = 1;
  private commandEnvelopeSeq = 1;

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
  ) {}

  async onModuleInit(): Promise<void> {
    // Initial sync of active groups on backend boot
    setTimeout(() => {
      this.syncAllActiveGroups().catch((err) => {
        this.logger.error(`Initial schedule sync error: ${err.message}`);
      });
    }, 3000);
  }

  /**
   * Listen to group assignment changes and downlink to all online gateways.
   */
  @OnEvent('group.assigned')
  async handleGroupAssigned(event: GroupAssignedEvent): Promise<void> {
    this.logger.log(
      `Received group.assigned for Group #${event.groupId}, treatmentVersionId=${event.treatmentVersionId}, nodes=[${event.nodeIds.join(',')}]`,
    );
    await this.syncGroup(event.groupId);
  }

  /**
   * Listen to group unassigned changes and downlink unassign to all online gateways.
   */
  @OnEvent('group.unassigned')
  async handleGroupUnassigned(event: GroupUnassignedEvent): Promise<void> {
    this.logger.log(`Received group.unassigned for Group #${event.groupId}`);
    const gateways = await this.getRelevantGateways();
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
      await this.syncGroup(assignment.group_id);
    }
  }

  /**
   * When an ESP32 gateway reconnects or reports status online, push all active group schedules to it.
   */
  @OnEvent(MQTT_EVENTS.DEVICE_STATUS)
  async handleDeviceStatus(data: { deviceId: string; payload: any }): Promise<void> {
    if (data.payload?.status === 'online') {
      this.logger.log(
        `Gateway ${data.deviceId} is online; synchronizing active group schedules...`,
      );
      await this.syncAllActiveGroups(data.deviceId);
    }
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
      // 1. Publish treatment config downlink
      const treatmentTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_TREATMENT_CONFIG(gw.device_id);
      const treatmentPayload = {
        command_id: randomUUID(),
        version: this.nextCommandEnvelopeVersion(),
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
        this.logger.log(
          `Published treatment config to ${treatmentTopic} (Group #${groupId}, v${version.version_num}, ${version.spray_day_s}s/${version.cooldown_day_s}s)`,
        );
      } catch (err: any) {
        this.logger.error(`Failed to publish treatment config: ${err.message}`);
      }

      // 2. Publish group-level authorization (ACTIVE) to the gateway.
      const groupStateTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_GROUP_STATE(gw.device_id);
      const groupStatePayload = {
        command_id: randomUUID(),
        version: this.nextCommandEnvelopeVersion(),
        group_id: groupId,
        active: true,
      };
      try {
        await this.mqttService.publish(groupStateTopic, groupStatePayload);
        this.logger.log(
          `Published ACTIVE group state to ${groupStateTopic} (Group #${groupId})`,
        );
      } catch (err: any) {
        this.logger.error(
          `Failed to publish group state to ${groupStateTopic} for Group #${groupId}: ${err.message}`,
        );
      }

      // 3. Publish node-to-group assignment for each node
      const assignmentTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_ASSIGNMENT_CONFIG(gw.device_id);
      for (const na of nodeAssignments) {
        const assignmentPayload = {
          command_id: randomUUID(),
          version: ++this.assignmentSeq,
          node_id: na.node_id,
          group_id: groupId,
        };

        try {
          await this.mqttService.publish(assignmentTopic, assignmentPayload);
          this.logger.log(
            `Published assignment to ${assignmentTopic} (Node #${na.node_id} -> Group #${groupId})`,
          );
        } catch (err: any) {
          this.logger.error(`Failed to publish assignment: ${err.message}`);
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

    for (const group of activeGroups) {
      await this.syncGroup(group.group_id, targetDeviceId);
    }
  }

  private async unassignGroupOnGateway(deviceId: string, groupId: number): Promise<void> {
    const groupStateTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_GROUP_STATE(deviceId);
    try {
      await this.mqttService.publish(groupStateTopic, {
        command_id: randomUUID(),
        version: this.nextCommandEnvelopeVersion(),
        group_id: groupId,
        active: false,
      });
      this.logger.log(`Published UNASSIGNED group state to ${groupStateTopic} (Group #${groupId})`);
    } catch (err: any) {
      this.logger.error(
        `Failed to publish group unassign to ${groupStateTopic} for Group #${groupId}: ${err.message}`,
      );
    }

    const nodeAssignments = await this.groupNodeRepo.find({
      where: { group_id: groupId },
    });

    const assignmentTopic = MQTT_PUBLISH_TEMPLATES.GATEWAY_ASSIGNMENT_CONFIG(deviceId);
    for (const na of nodeAssignments) {
      const assignmentPayload = {
        command_id: randomUUID(),
        version: ++this.assignmentSeq,
        node_id: na.node_id,
        group_id: 0, // 0 = UNASSIGNED
      };
      try {
        await this.mqttService.publish(assignmentTopic, assignmentPayload);
      } catch (err: any) {
        this.logger.error(`Failed to publish unassign: ${err.message}`);
      }
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
}

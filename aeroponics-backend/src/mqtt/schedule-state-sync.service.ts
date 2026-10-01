import { Injectable, Logger, NotFoundException, ServiceUnavailableException } from '@nestjs/common';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { randomUUID } from 'crypto';

import { ControlSlot, ControlSlotTargetType } from '../control-slot/entities/control_slot.entity';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { Device } from '../device/entities/device.entity';
import { GroupTreatmentAssignment } from '../group/entities/group_treatment_assignment.entity';
import { TreatmentVersion } from '../treatment/entities/treatment_version.entity';
import { GroupScheduleSyncService } from './group-schedule-sync.service';
import { MQTT_EVENTS } from './mqtt.constants';

export const SCHEDULE_SYNC_STATES = {
  IN_SYNC: 'IN_SYNC',
  IN_SYNC_PENDING_BOUNDARY: 'IN_SYNC_PENDING_BOUNDARY',
  SYNCING: 'SYNCING',
  DRIFTED: 'DRIFTED',
  DRIFTED_LATCHED: 'DRIFTED_LATCHED',
  UNCONFIRMED: 'UNCONFIRMED',
} as const;

/**
 * A gateway reports its schedule every 60s. Server receive time is the only
 * freshness authority, so a missing report past this budget means the last
 * snapshot no longer describes the hardware.
 */
export const SCHEDULE_SYNC_STALE_MS = 180000;

type SyncState = (typeof SCHEDULE_SYNC_STATES)[keyof typeof SCHEDULE_SYNC_STATES];
type RetryState = { attempts: number[]; syncing: boolean; latched: boolean };
type PendingCommand = { deviceId: string; groupId: number; commandType: string; attemptId: string; timer: NodeJS.Timeout };

@Injectable()
export class ScheduleStateSyncService {
  private readonly logger = new Logger(ScheduleStateSyncService.name);
  private readonly retry = new Map<string, RetryState>();
  private readonly lastAttemptAt = new Map<string, number>();
  private readonly pendingCommands = new Map<string, PendingCommand>();
  private readonly attemptIds = new Map<string, string>();
  private readonly reportTimers = new Map<string, NodeJS.Timeout>();

  constructor(
    @InjectRepository(DeviceStatus)
    private readonly deviceStatusRepo: Repository<DeviceStatus>,
    @InjectRepository(Device)
    private readonly deviceRepo: Repository<Device>,
    @InjectRepository(ControlSlot)
    private readonly controlSlotRepo: Repository<ControlSlot>,
    @InjectRepository(GroupTreatmentAssignment)
    private readonly groupTreatmentRepo: Repository<GroupTreatmentAssignment>,
    @InjectRepository(TreatmentVersion)
    private readonly treatmentVersionRepo: Repository<TreatmentVersion>,
    private readonly groupScheduleSync: GroupScheduleSyncService,
    private readonly eventEmitter: EventEmitter2,
  ) {}

  @OnEvent(MQTT_EVENTS.DEVICE_SCHEDULE_STATE)
  async handleScheduleState(event: { deviceId: string; payload: any; receivedAt?: Date }): Promise<void> {
    if (!this.isValidReport(event?.deviceId, event?.payload)) return;
    const receivedAt = event.receivedAt ?? new Date();
    const status = await this.deviceStatusRepo.findOne({ where: { device_id: event.deviceId } });
    if (!status) {
      this.logger.warn(`Ignoring schedule report for unknown device ${event.deviceId}`);
      return;
    }

    const comparison = await this.compareReport(event.deviceId, event.payload);
    const retry = this.getRetryState(event.deviceId);
    status.reported_schedule_state = event.payload;
    status.schedule_sync_updated_at = receivedAt;

    if (comparison.state === SCHEDULE_SYNC_STATES.IN_SYNC) {
      retry.attempts = [];
      retry.latched = false;
      this.lastAttemptAt.delete(event.deviceId);
      this.clearPendingCommands(event.deviceId);
    }

    status.schedule_sync_state = comparison.state;
    status.schedule_sync_details = {
      driftedGroups: comparison.driftedGroups,
      boundaryGroups: comparison.boundaryGroups,
      receivedAt: receivedAt.toISOString(),
    };
    await this.deviceStatusRepo.save(status);
    this.emitChanged(event.deviceId, status);

    if (comparison.state === SCHEDULE_SYNC_STATES.DRIFTED || comparison.state === SCHEDULE_SYNC_STATES.SYNCING) {
      await this.tryAutoResync(event.deviceId, comparison.driftedGroups);
    }
  }

  async retryManually(deviceId: string): Promise<{ device_id: string; syncState: string; attemptId: string; pendingGroups: number[]; message: string }> {
    const device = await this.deviceRepo.findOne({ where: { device_id: deviceId } });
    if (!device) throw new NotFoundException(`Device not found for device: ${deviceId}`);
    let status = await this.deviceStatusRepo.findOne({ where: { device_id: deviceId } });
    if (!status) status = this.deviceStatusRepo.create({ device_id: deviceId, status: 'online', uptime_s: '0', ntp_synced: false, rtc_valid: false, last_seen_at: new Date() });
    const retry = this.getRetryState(deviceId);
    const attemptId = randomUUID();
    this.attemptIds.set(deviceId, attemptId);
    retry.attempts = [];
    retry.latched = false;
    this.lastAttemptAt.delete(deviceId);
    status.schedule_sync_state = SCHEDULE_SYNC_STATES.SYNCING;
    status.schedule_sync_updated_at = new Date();
    await this.deviceStatusRepo.save(status);
    this.emitChanged(deviceId, status);
    const comparison = status.reported_schedule_state
      ? await this.compareReport(deviceId, status.reported_schedule_state)
      : null;
    const groupsToSync = comparison
      ? comparison.driftedGroups
      : await this.getDesiredGroupIds(deviceId);
    const syncError = await this.tryResync(deviceId, groupsToSync, true);
    if (syncError) {
      throw new ServiceUnavailableException(`Schedule sync could not be published: ${syncError.message}`);
    }
    return { device_id: deviceId, syncState: status.schedule_sync_state, attemptId, pendingGroups: groupsToSync, message: 'Schedule sync started' };
  }

  @OnEvent('schedule.sync.command_published')
  handleCommandPublished(event: { deviceId: string; groupId: number; commandType: string; commandId: string }): void {
    const attemptId = this.attemptIds.get(event.deviceId);
    if (!attemptId) return;
    const timer = setTimeout(() => {
      const pending = this.pendingCommands.get(event.commandId);
      if (!pending) return;
      this.pendingCommands.delete(event.commandId);
      void this.markSyncFailure(event.deviceId, {
        reason: 'COMMAND_TIMEOUT', commandId: event.commandId, groupId: event.groupId,
        commandType: event.commandType,
      });
    }, 15000);
    this.pendingCommands.set(event.commandId, { ...event, attemptId, timer });
  }

  @OnEvent('schedule.sync.command_ack')
  handleCommandAck(event: { deviceId?: string; commandId: string; status: string; reason?: string | null }): void {
    const pending = this.pendingCommands.get(event.commandId);
    if (!pending || (event.deviceId && event.deviceId !== pending.deviceId)) return;
    if (event.status === 'ACCEPTED' || event.status === 'COMPLETED') {
      clearTimeout(pending.timer);
      this.pendingCommands.delete(event.commandId);
      return;
    }
    if (event.status === 'REJECTED' || event.status === 'FAULT') {
      clearTimeout(pending.timer);
      this.pendingCommands.delete(event.commandId);
      void this.markSyncFailure(pending.deviceId, {
        reason: 'FIRMWARE_REJECTED', commandId: event.commandId, groupId: pending.groupId,
        commandType: pending.commandType, ackReason: event.reason ?? event.status,
      });
    }
  }

  async getDesiredGroupState(deviceId: string, groupId: number): Promise<any> {
    const slots = await this.controlSlotRepo.find({
      where: { device_id: deviceId, target_type: ControlSlotTargetType.GROUP, target_id: groupId },
    });
    if (slots.length === 0) return { active: false, profile: null };

    const assignment = await this.groupTreatmentRepo.findOne({
      where: { group_id: groupId, active: true },
      order: { assigned_at: 'DESC' },
    });
    if (!assignment) return { active: false, profile: null };
    const version = await this.treatmentVersionRepo.findOne({ where: { id: assignment.treatment_version_id } });
    if (!version) return { active: false, profile: null };
    return {
      active: true,
      profile: {
        treatment_version: version.version_num,
        spray_day_s: version.spray_day_s,
        cooldown_day_s: version.cooldown_day_s,
        spray_night_s: version.spray_night_s,
        cooldown_night_s: version.cooldown_night_s,
      },
    };
  }

  private async compareReport(deviceId: string, report: any): Promise<{
    state: SyncState;
    driftedGroups: number[];
    boundaryGroups: number[];
  }> {
    const reportedGroups = Array.isArray(report.groups) ? report.groups : [];
    const reportedGroupSlots = new Set<number>(
      (Array.isArray(report.active_slots) ? report.active_slots : [])
        .filter((slot: any) => String(slot?.type ?? '').toUpperCase() === 'GROUP')
        .map((slot: any) => Number(slot?.id))
        .filter((groupId: number) => Number.isInteger(groupId) && groupId >= 1 && groupId <= 4),
    );
    const driftedGroups: number[] = [];
    const boundaryGroups: number[] = [];
    for (let groupId = 1; groupId <= 4; groupId += 1) {
      const actual = reportedGroups.find((g: any) => Number(g?.group_id) === groupId);
      const desired = await this.getDesiredGroupState(deviceId, groupId);
      const actualActive = String(actual?.state ?? 'UNASSIGNED').toUpperCase() === 'ACTIVE';
      if (!desired.active) {
        if (actualActive || reportedGroupSlots.has(groupId)) driftedGroups.push(groupId);
        continue;
      }
      if (!actualActive || !reportedGroupSlots.has(groupId)) {
        driftedGroups.push(groupId);
        continue;
      }
      if (!desired.profile) continue;
      const active = actual?.profile?.active;
      const pending = actual?.profile?.pending;
      if (this.profileMatches(active, desired.profile)) continue;
      if (pending?.has_pending === true && this.profileMatches(pending, desired.profile)) {
        boundaryGroups.push(groupId);
      } else {
        driftedGroups.push(groupId);
      }
    }

    if (driftedGroups.length > 0) {
      const retry = this.getRetryState(deviceId);
      return { state: retry.latched ? SCHEDULE_SYNC_STATES.DRIFTED_LATCHED : SCHEDULE_SYNC_STATES.DRIFTED, driftedGroups, boundaryGroups };
    }
    if (boundaryGroups.length > 0) return { state: SCHEDULE_SYNC_STATES.IN_SYNC_PENDING_BOUNDARY, driftedGroups, boundaryGroups };
    return { state: SCHEDULE_SYNC_STATES.IN_SYNC, driftedGroups, boundaryGroups };
  }

  private profileMatches(actual: any, desired: any): boolean {
    if (!actual || !desired) return false;
    return ['treatment_version', 'spray_day_s', 'cooldown_day_s', 'spray_night_s', 'cooldown_night_s']
      .every((key) => Number(actual[key]) === Number(desired[key]));
  }

  private async tryAutoResync(deviceId: string, groupIds: number[]): Promise<void> {
    await this.tryResync(deviceId, groupIds, false);
  }

  private async getDesiredGroupIds(deviceId: string): Promise<number[]> {
    const groupIds: number[] = [];
    for (let groupId = 1; groupId <= 4; groupId += 1) {
      if ((await this.getDesiredGroupState(deviceId, groupId)).active) groupIds.push(groupId);
    }
    return groupIds;
  }

  /**
   * Downgrade schedule states whose last gateway report is older than the
   * freshness budget. `DRIFTED_LATCHED` is preserved: the latch is a hardware
   * failure record and must not be cleared by an unrelated heartbeat gap.
   */
  async checkScheduleSyncFreshness(timeoutMs: number = SCHEDULE_SYNC_STALE_MS): Promise<void> {
    const now = Date.now();
    const statuses = await this.deviceStatusRepo.find();
    for (const status of statuses) {
      if (!status.device_id || status.device_id === 'esp32_device') continue;
      const currentState = status.schedule_sync_state;
      if (!currentState || currentState === SCHEDULE_SYNC_STATES.UNCONFIRMED) continue;
      if (currentState === SCHEDULE_SYNC_STATES.DRIFTED_LATCHED) continue;

      const lastReportAt = status.schedule_sync_updated_at
        ? new Date(status.schedule_sync_updated_at).getTime()
        : 0;
      if (lastReportAt > 0 && now - lastReportAt <= timeoutMs) continue;

      this.logger.warn(
        `Gateway ${status.device_id} schedule report is stale (last received ${
          lastReportAt > 0 ? `${Math.floor((now - lastReportAt) / 1000)}s ago` : 'never'
        }); marking UNCONFIRMED.`,
      );
      status.schedule_sync_state = SCHEDULE_SYNC_STATES.UNCONFIRMED;
      status.schedule_sync_details = {
        reason: 'REPORT_STALE',
        lastReportAt: lastReportAt > 0 ? new Date(lastReportAt).toISOString() : null,
        timeoutMs,
      };
      await this.deviceStatusRepo.save(status);
      this.emitChanged(status.device_id, status);
    }
  }

  private async tryResync(deviceId: string, groupIds: number[], manual: boolean): Promise<Error | null> {
    const retry = this.getRetryState(deviceId);
    if (retry.syncing || retry.latched) return null;
    const now = Date.now();
    const attempts = retry.attempts.filter((time) => now - time < 60_000);
    retry.attempts = attempts;
    if (!manual && this.lastAttemptAt.has(deviceId) && now - (this.lastAttemptAt.get(deviceId) as number) < 10_000) return null;
    if (!manual && attempts.length >= 3) {
      retry.latched = true;
      await this.setState(deviceId, SCHEDULE_SYNC_STATES.DRIFTED_LATCHED, { reason: 'AUTO_RETRY_LIMIT', attempts: attempts.length });
      return null;
    }
    retry.syncing = true;
    retry.attempts.push(now);
    this.lastAttemptAt.set(deviceId, now);
    if (!this.attemptIds.has(deviceId)) this.attemptIds.set(deviceId, randomUUID());
    await this.setState(deviceId, SCHEDULE_SYNC_STATES.SYNCING, { driftedGroups: groupIds });
    let syncError: Error | null = null;
    try {
      for (const groupId of groupIds) {
        const desired = await this.getDesiredGroupState(deviceId, groupId);
        if (desired.active) {
          await this.groupScheduleSync.syncGroup(groupId, deviceId);
        } else {
          // A group absent from control-slots must be explicitly disabled on
          // the gateway. There may be no treatment assignment for it, so
          // routing it through syncGroup() would silently do nothing.
          await this.groupScheduleSync.unassignGroupOnGateway(deviceId, groupId);
        }
      }
      this.scheduleReportTimeout(deviceId, this.attemptIds.get(deviceId));
    } catch (error: any) {
      syncError = error instanceof Error ? error : new Error(String(error));
      await this.markSyncFailure(deviceId, { reason: 'PUBLISH_FAILED', message: syncError.message });
    } finally {
      retry.syncing = false;
    }
    return syncError;
  }

  private async markSyncFailure(deviceId: string, details: any): Promise<void> {
    const retry = this.getRetryState(deviceId);
    retry.syncing = false;
    this.clearPendingCommands(deviceId);
    await this.setState(deviceId, SCHEDULE_SYNC_STATES.DRIFTED, details);
  }

  private clearPendingCommands(deviceId: string): void {
    for (const [commandId, pending] of this.pendingCommands) {
      if (pending.deviceId === deviceId) {
        clearTimeout(pending.timer);
        this.pendingCommands.delete(commandId);
      }
    }
    const reportTimer = this.reportTimers.get(deviceId);
    if (reportTimer) {
      clearTimeout(reportTimer);
      this.reportTimers.delete(deviceId);
    }
    this.attemptIds.delete(deviceId);
  }

  private scheduleReportTimeout(deviceId: string, attemptId: string | undefined): void {
    if (!attemptId) return;
    const existing = this.reportTimers.get(deviceId);
    if (existing) clearTimeout(existing);
    const timer = setTimeout(() => {
      if (this.attemptIds.get(deviceId) !== attemptId) return;
      void this.markSyncFailure(deviceId, {
        reason: 'SCHEDULE_REPORT_TIMEOUT',
        message: 'No matching schedule report received from Gateway',
      });
    }, 30_000);
    timer.unref?.();
    this.reportTimers.set(deviceId, timer);
  }

  private async setState(deviceId: string, state: SyncState, details: any): Promise<void> {
    const status = await this.deviceStatusRepo.findOne({ where: { device_id: deviceId } });
    if (!status) return;
    status.schedule_sync_state = state;
    status.schedule_sync_updated_at = new Date();
    status.schedule_sync_details = details;
    await this.deviceStatusRepo.save(status);
    this.emitChanged(deviceId, status);
  }

  private emitChanged(deviceId: string, status: DeviceStatus): void {
    this.eventEmitter.emit('device.schedule_sync_changed', this.toEvent(deviceId, status));
  }

  private toEvent(deviceId: string, status: DeviceStatus): any {
    return { deviceId, syncState: status.schedule_sync_state ?? SCHEDULE_SYNC_STATES.UNCONFIRMED, reportedScheduleState: status.reported_schedule_state, scheduleSyncUpdatedAt: status.schedule_sync_updated_at?.toISOString() ?? null, scheduleSyncDetails: status.schedule_sync_details };
  }

  private getRetryState(deviceId: string): RetryState {
    let state = this.retry.get(deviceId);
    if (!state) {
      state = { attempts: [], syncing: false, latched: false };
      this.retry.set(deviceId, state);
    }
    return state;
  }

  private isValidReport(deviceId: string, payload: any): boolean {
    return typeof deviceId === 'string' && deviceId.length > 0 && payload && typeof payload === 'object' && Array.isArray(payload.groups);
  }
}

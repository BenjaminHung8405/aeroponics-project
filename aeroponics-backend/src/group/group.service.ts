import {
  Injectable,
  NotFoundException,
  BadRequestException,
  ConflictException,
  Logger,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, DataSource, In } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { DateTime } from 'luxon';

import { TimerGroup, TimerGroupStatus } from './entities/timer_group.entity';
import { GroupTreatmentAssignment } from './entities/group_treatment_assignment.entity';
import { GroupNodeAssignment } from './entities/group_node_assignment.entity';
import {
  TreatmentVersion,
  TreatmentVersionStatus,
} from '../treatment/entities/treatment_version.entity';
import { NodeRegistry } from '../node/entities/node_registry.entity';
import { SeasonService } from '../season/season.service';
import { AssignGroupDto } from './dto/assign-group.dto';
import { GroupAssignedEvent, GroupUnassignedEvent } from './events/group.events';
import {
  CyclePhase,
  GroupStatusResponse,
  GroupNodeSummary,
  GroupTreatmentSummary,
} from './group.types';
import { isModernNodeId } from '../node/node-topology';

export const ICT_TIMEZONE = 'Asia/Ho_Chi_Minh';

@Injectable()
export class GroupService {
  private readonly logger = new Logger(GroupService.name);

  constructor(
    @InjectRepository(TimerGroup)
    private readonly groupRepository: Repository<TimerGroup>,
    @InjectRepository(GroupTreatmentAssignment)
    private readonly treatmentAssignmentRepository: Repository<GroupTreatmentAssignment>,
    @InjectRepository(GroupNodeAssignment)
    private readonly nodeAssignmentRepository: Repository<GroupNodeAssignment>,
    @InjectRepository(TreatmentVersion)
    private readonly treatmentVersionRepository: Repository<TreatmentVersion>,
    @InjectRepository(NodeRegistry)
    private readonly nodeRegistryRepository: Repository<NodeRegistry>,
    private readonly seasonService: SeasonService,
    private readonly dataSource: DataSource,
    private readonly eventEmitter: EventEmitter2,
  ) {}

  /**
   * Calculate current phase (DAY/NIGHT) and next transition time based on ICT timezone (Asia/Ho_Chi_Minh).
   * Day boundary: 06:00:00 - 17:59:59.999 ICT
   * Night boundary: 18:00:00 - 05:59:59.999 ICT
   */
  calculateCurrentPhase(atDate?: Date): {
    phase: CyclePhase;
    nextTransitionAt: string;
  } {
    const dt = (atDate ? DateTime.fromJSDate(atDate) : DateTime.now()).setZone(
      ICT_TIMEZONE,
    );

    const secondsOfDay =
      dt.hour * 3600 + dt.minute * 60 + dt.second + dt.millisecond / 1000;

    // 06:00:00 ICT = 21600 seconds; 18:00:00 ICT = 64800 seconds
    const isDay = secondsOfDay >= 21600 && secondsOfDay < 64800;
    const phase: CyclePhase = isDay ? 'DAY' : 'NIGHT';

    let nextTransition: DateTime;
    if (isDay) {
      nextTransition = dt.set({
        hour: 18,
        minute: 0,
        second: 0,
        millisecond: 0,
      });
    } else if (secondsOfDay < 21600) {
      nextTransition = dt.set({
        hour: 6,
        minute: 0,
        second: 0,
        millisecond: 0,
      });
    } else {
      nextTransition = dt
        .plus({ days: 1 })
        .set({ hour: 6, minute: 0, second: 0, millisecond: 0 });
    }

    return {
      phase,
      nextTransitionAt: nextTransition.toISO() ?? nextTransition.toString(),
    };
  }

  /**
   * Assign a published treatment version and set of nodes to a timer group.
   */
  async assignTreatmentVersion(
    groupId: number,
    dto: AssignGroupDto,
  ): Promise<GroupStatusResponse> {
    this.validateGroupId(groupId);

    for (const nodeId of dto.node_ids) {
      if (!isModernNodeId(nodeId)) {
        throw new BadRequestException(`Node ID must be between 1 and 15. Received: ${nodeId}`);
      }
    }

    const group = await this.groupRepository.findOne({
      where: { group_id: groupId },
    });
    if (!group) {
      throw new NotFoundException(`Timer group #${groupId} not found.`);
    }

    const activeSeason = await this.seasonService.getActive();
    if (!activeSeason) {
      throw new BadRequestException(
        'Cannot assign group: No active season found. Please create an active season first.',
      );
    }

    const version = await this.treatmentVersionRepository.findOne({
      where: { id: dto.treatment_version_id },
      relations: ['treatment'],
    });
    if (!version) {
      throw new NotFoundException(
        `Treatment version #${dto.treatment_version_id} not found.`,
      );
    }

    if (version.status !== TreatmentVersionStatus.PUBLISHED) {
      throw new BadRequestException(
        `Treatment version #${dto.treatment_version_id} is in status '${version.status}'. Only PUBLISHED versions can be assigned to a group.`,
      );
    }

    // Invariant check: ensure none of the requested nodes are actively assigned to another group
    const conflictingAssignments = await this.nodeAssignmentRepository.find({
      where: {
        season_id: activeSeason.id,
        node_id: In(dto.node_ids),
        active: true,
      },
    });

    const foreignConflicts = conflictingAssignments.filter(
      (a) => a.group_id !== groupId && a.effective_to === null,
    );
    if (foreignConflicts.length > 0) {
      const conflictDetails = foreignConflicts
        .map((a) => `Node ${a.node_id} (assigned to Group ${a.group_id})`)
        .join(', ');
      throw new ConflictException(
        `Assignment conflict: The following nodes are already actively assigned to another group in this season: ${conflictDetails}. Unassign them first.`,
      );
    }

    const now = new Date();

    // Execute assignment change atomically inside a transaction
    await this.dataSource.transaction(async (manager) => {
      // 1. Deactivate existing active treatment assignment for this group in this season
      await manager
        .createQueryBuilder()
        .update(GroupTreatmentAssignment)
        .set({ active: false, unassigned_at: now })
        .where(
          'group_id = :groupId AND season_id = :seasonId AND active = true AND unassigned_at IS NULL',
          { groupId, seasonId: activeSeason.id },
        )
        .execute();

      // 2. Insert new GroupTreatmentAssignment
      const newTreatmentAssignment = manager.create(GroupTreatmentAssignment, {
        group_id: groupId,
        treatment_version_id: dto.treatment_version_id,
        season_id: activeSeason.id,
        active: true,
        assigned_at: now,
        unassigned_at: null,
      });
      await manager.save(newTreatmentAssignment);

      // 3. Deactivate previous active node assignments for this group
      await manager
        .createQueryBuilder()
        .update(GroupNodeAssignment)
        .set({ active: false, effective_to: now })
        .where(
          'group_id = :groupId AND season_id = :seasonId AND active = true AND effective_to IS NULL',
          { groupId, seasonId: activeSeason.id },
        )
        .execute();

      // 4. Create new GroupNodeAssignment for each assigned node
      const newNodeAssignments = dto.node_ids.map((nodeId) =>
        manager.create(GroupNodeAssignment, {
          group_id: groupId,
          node_id: nodeId,
          season_id: activeSeason.id,
          active: true,
          effective_from: now,
          effective_to: null,
        }),
      );
      await manager.save(newNodeAssignments);

      // 5. Update cached_group_id in NodeRegistry:
      // Set cached_group_id = groupId for assigned nodes
      await manager
        .createQueryBuilder()
        .update(NodeRegistry)
        .set({ cached_group_id: groupId })
        .where('node_id IN (:...nodeIds)', { nodeIds: dto.node_ids })
        .execute();

      // Reset cached_group_id = null for nodes previously belonging to this group that were unassigned
      await manager
        .createQueryBuilder()
        .update(NodeRegistry)
        .set({ cached_group_id: null })
        .where(
          'cached_group_id = :groupId AND node_id NOT IN (:...nodeIds)',
          { groupId, nodeIds: dto.node_ids },
        )
        .execute();

      // 6. Update TimerGroup status to ACTIVE
      await manager
        .createQueryBuilder()
        .update(TimerGroup)
        .set({ status: TimerGroupStatus.ACTIVE, updated_at: now })
        .where('group_id = :groupId', { groupId })
        .execute();
    });

    this.logger.log(
      `Group #${groupId} successfully assigned with TreatmentVersion #${dto.treatment_version_id} and Nodes [${dto.node_ids.join(', ')}].`,
    );

    this.eventEmitter.emit(
      'group.assigned',
      new GroupAssignedEvent(
        groupId,
        activeSeason.id,
        dto.treatment_version_id,
        dto.node_ids,
        now,
      ),
    );

    return this.getGroupStatus(groupId);
  }

  /**
   * Unassign a timer group, deactivating treatment and node assignments.
   */
  async unassign(groupId: number): Promise<GroupStatusResponse> {
    this.validateGroupId(groupId);

    const group = await this.groupRepository.findOne({
      where: { group_id: groupId },
    });
    if (!group) {
      throw new NotFoundException(`Timer group #${groupId} not found.`);
    }

    const activeSeason = await this.seasonService.getActive();
    const now = new Date();

    await this.dataSource.transaction(async (manager) => {
      // 1. Deactivate active treatment assignments
      await manager
        .createQueryBuilder()
        .update(GroupTreatmentAssignment)
        .set({ active: false, unassigned_at: now })
        .where('group_id = :groupId AND active = true AND unassigned_at IS NULL', {
          groupId,
        })
        .execute();

      // 2. Deactivate active node assignments
      await manager
        .createQueryBuilder()
        .update(GroupNodeAssignment)
        .set({ active: false, effective_to: now })
        .where('group_id = :groupId AND active = true AND effective_to IS NULL', {
          groupId,
        })
        .execute();

      // 3. Clear cached_group_id on node registry
      await manager
        .createQueryBuilder()
        .update(NodeRegistry)
        .set({ cached_group_id: null })
        .where('cached_group_id = :groupId', { groupId })
        .execute();

      // 4. Update TimerGroup status to UNASSIGNED
      await manager
        .createQueryBuilder()
        .update(TimerGroup)
        .set({ status: TimerGroupStatus.UNASSIGNED, updated_at: now })
        .where('group_id = :groupId', { groupId })
        .execute();
    });

    this.logger.log(`Group #${groupId} successfully unassigned.`);

    this.eventEmitter.emit(
      'group.unassigned',
      new GroupUnassignedEvent(groupId, activeSeason?.id ?? null, now),
    );

    return this.getGroupStatus(groupId);
  }

  /**
   * Get status of all 4 groups, or a specific group.
   */
  async getGroupStatus(groupId?: number): Promise<GroupStatusResponse> {
    if (groupId !== undefined) {
      this.validateGroupId(groupId);
      const group = await this.groupRepository.findOne({
        where: { group_id: groupId },
      });
      if (!group) {
        throw new NotFoundException(`Timer group #${groupId} not found.`);
      }
      return this.buildGroupStatusResponse(group);
    }

    const groups = await this.groupRepository.find({
      order: { group_id: 'ASC' },
    });
    if (groups.length === 0) {
      throw new NotFoundException('No timer groups found in registry.');
    }

    return this.buildGroupStatusResponse(groups[0]);
  }

  /**
   * Get all 4 groups status.
   */
  async getAllGroupsStatus(): Promise<GroupStatusResponse[]> {
    const groups = await this.groupRepository.find({
      order: { group_id: 'ASC' },
    });

    return Promise.all(groups.map((g) => this.buildGroupStatusResponse(g)));
  }

  private async buildGroupStatusResponse(
    group: TimerGroup,
  ): Promise<GroupStatusResponse> {
    const activeTreatmentAssign =
      await this.treatmentAssignmentRepository.findOne({
        where: { group_id: group.group_id, active: true },
        relations: ['treatment_version', 'treatment_version.treatment'],
        order: { assigned_at: 'DESC' },
      });

    let treatmentSummary: GroupTreatmentSummary | null = null;
    if (activeTreatmentAssign?.treatment_version) {
      const tv = activeTreatmentAssign.treatment_version;
      treatmentSummary = {
        treatment_id: tv.treatment?.id ?? 0,
        treatment_name: tv.treatment?.name ?? 'Unknown',
        treatment_version_id: tv.id,
        version_num: tv.version_num,
        spray_day_s: tv.spray_day_s,
        cooldown_day_s: tv.cooldown_day_s,
        spray_night_s: tv.spray_night_s,
        cooldown_night_s: tv.cooldown_night_s,
      };
    }

    const activeNodeAssigns =
      (await this.nodeAssignmentRepository.find({
        where: { group_id: group.group_id, active: true },
        order: { node_id: 'ASC' },
      })) ?? [];

    const nodeIds = activeNodeAssigns.map((na) => na.node_id);
    let nodeSummaries: GroupNodeSummary[] = [];

    if (nodeIds.length > 0) {
      const nodes = await this.nodeRegistryRepository.find({
        where: { node_id: In(nodeIds) },
        order: { node_id: 'ASC' },
      });

      const now = Date.now();
      const STALE_MS = 120000;

      nodeSummaries = nodes.map((n) => ({
        node_id: n.node_id,
        display_name: n.display_name,
        health_status: n.health_status,
        schedule_state: n.schedule_state,
        last_seen_at: n.last_seen_at,
        is_stale: n.last_seen_at
          ? now - new Date(n.last_seen_at).getTime() > STALE_MS
          : true,
      }));
    }

    const isGroupActive = group.status === TimerGroupStatus.ACTIVE;
    const phaseInfo = isGroupActive ? this.calculateCurrentPhase() : null;

    return {
      group_id: group.group_id,
      name: group.name,
      status: group.status,
      current_phase: phaseInfo ? phaseInfo.phase : null,
      next_transition_at: phaseInfo ? phaseInfo.nextTransitionAt : null,
      treatment: treatmentSummary,
      nodes: nodeSummaries,
    };
  }

  private validateGroupId(groupId: number): void {
    if (!Number.isInteger(groupId) || groupId < 1 || groupId > 4) {
      throw new BadRequestException('Group ID must be an integer between 1 and 4.');
    }
  }
}

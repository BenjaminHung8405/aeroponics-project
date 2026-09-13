import { TimerGroupStatus } from './entities/timer_group.entity';
import { NodeHealthStatus, ScheduleState } from '../node/entities/node_registry.entity';

export type CyclePhase = 'DAY' | 'NIGHT';

export interface GroupNodeSummary {
  node_id: number;
  display_name: string;
  health_status: NodeHealthStatus;
  schedule_state: ScheduleState;
  last_seen_at: Date | null;
  is_stale: boolean;
}

export interface GroupTreatmentSummary {
  treatment_id: number;
  treatment_name: string;
  treatment_version_id: number;
  version_num: number;
  spray_day_s: number;
  cooldown_day_s: number;
  spray_night_s: number;
  cooldown_night_s: number;
}

export interface GroupStatusResponse {
  group_id: number;
  name: string;
  status: TimerGroupStatus;
  current_phase: CyclePhase | null;
  next_transition_at: string | null;
  treatment: GroupTreatmentSummary | null;
  nodes: GroupNodeSummary[];
}

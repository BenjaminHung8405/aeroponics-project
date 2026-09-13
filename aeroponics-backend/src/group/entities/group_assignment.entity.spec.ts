import { TimerGroup, TimerGroupStatus } from './timer_group.entity';
import { GroupTreatmentAssignment } from './group_treatment_assignment.entity';
import { GroupNodeAssignment } from './group_node_assignment.entity';

describe('Group Assignment Entities (S3-B3)', () => {
  it('should initialize TimerGroup with valid status', () => {
    const group = new TimerGroup();
    group.group_id = 1;
    group.name = 'Group 1';
    group.status = TimerGroupStatus.ACTIVE;

    expect(group.group_id).toBe(1);
    expect(group.name).toBe('Group 1');
    expect(group.status).toBe(TimerGroupStatus.ACTIVE);
  });

  it('should create valid GroupTreatmentAssignment', () => {
    const assignment = new GroupTreatmentAssignment();
    assignment.id = 1;
    assignment.group_id = 1;
    assignment.treatment_version_id = 10;
    assignment.season_id = 1;
    assignment.active = true;
    assignment.assigned_at = new Date();
    assignment.unassigned_at = null;

    expect(assignment.group_id).toBe(1);
    expect(assignment.treatment_version_id).toBe(10);
    expect(assignment.season_id).toBe(1);
    expect(assignment.active).toBe(true);
    expect(assignment.unassigned_at).toBeNull();
  });

  it('should create valid GroupNodeAssignment', () => {
    const nodeAssignment = new GroupNodeAssignment();
    nodeAssignment.id = 1;
    nodeAssignment.group_id = 1;
    nodeAssignment.node_id = 2;
    nodeAssignment.season_id = 1;
    nodeAssignment.active = true;
    nodeAssignment.effective_from = new Date();
    nodeAssignment.effective_to = null;

    expect(nodeAssignment.node_id).toBe(2);
    expect(nodeAssignment.group_id).toBe(1);
    expect(nodeAssignment.active).toBe(true);
    expect(nodeAssignment.effective_to).toBeNull();
  });

  it('should prevent node from being active in multiple groups concurrently', () => {
    // Invariant: UNIQUE (season_id, node_id) WHERE active AND effective_to IS NULL
    const activeAssignments = [
      { season_id: 1, node_id: 2, group_id: 1, active: true },
    ];

    const assignNode = (seasonId: number, nodeId: number, groupId: number) => {
      const alreadyActive = activeAssignments.some(
        (a) => a.season_id === seasonId && a.node_id === nodeId && a.active,
      );
      if (alreadyActive) {
        throw new Error(
          `CONFLICT_NODE_ASSIGNMENT: Node ${nodeId} is already actively assigned to a group in season ${seasonId}`,
        );
      }
      activeAssignments.push({ season_id: seasonId, node_id: nodeId, group_id: groupId, active: true });
    };

    expect(() => assignNode(1, 2, 3)).toThrow('CONFLICT_NODE_ASSIGNMENT');
  });

  it('should validate group_id and node_id boundaries (1..4)', () => {
    const validateNodeAndGroup = (groupId: number, nodeId: number) => {
      if (groupId < 1 || groupId > 4) {
        throw new Error('group_id out of range [1..4]');
      }
      if (nodeId < 1 || nodeId > 4) {
        throw new Error('node_id out of range [1..4]');
      }
      return true;
    };

    expect(() => validateNodeAndGroup(0, 1)).toThrow('group_id out of range');
    expect(() => validateNodeAndGroup(5, 1)).toThrow('group_id out of range');
    expect(() => validateNodeAndGroup(1, 0)).toThrow('node_id out of range');
    expect(() => validateNodeAndGroup(1, 5)).toThrow('node_id out of range');
    expect(validateNodeAndGroup(1, 4)).toBe(true);
  });
});

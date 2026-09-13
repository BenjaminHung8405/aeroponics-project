/**
 * Composite group assignment definition used for high-level APIs and domain services.
 * While the underlying database normalizes into `group_treatment_assignments` and `group_node_assignments`
 * to strictly prevent race conditions and enforce partial unique constraints,
 * this structure provides a unified representation of a group's active configuration.
 */
export interface GroupAssignmentSummary {
  groupId: number;
  treatmentVersionId: number;
  seasonId: number;
  nodeIds: number[];
  assignedAt: Date;
  active: boolean;
}

export interface AssignGroupPayload {
  groupId: number;
  treatmentVersionId: number;
  seasonId: number;
  nodeIds: number[];
}

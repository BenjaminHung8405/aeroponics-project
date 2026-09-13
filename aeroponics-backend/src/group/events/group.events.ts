export class GroupAssignedEvent {
  constructor(
    public readonly groupId: number,
    public readonly seasonId: number,
    public readonly treatmentVersionId: number,
    public readonly nodeIds: number[],
    public readonly assignedAt: Date,
  ) {}
}

export class GroupUnassignedEvent {
  constructor(
    public readonly groupId: number,
    public readonly seasonId: number | null,
    public readonly unassignedAt: Date,
  ) {}
}

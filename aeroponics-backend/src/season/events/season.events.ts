export class SeasonCreatedEvent {
  constructor(
    public readonly seasonId: number,
    public readonly name: string,
    public readonly startedAt: Date,
  ) {}
}

export class SeasonEndedEvent {
  constructor(
    public readonly seasonId: number,
    public readonly name: string,
    public readonly endedAt: Date,
    public readonly notes?: string | null,
  ) {}
}

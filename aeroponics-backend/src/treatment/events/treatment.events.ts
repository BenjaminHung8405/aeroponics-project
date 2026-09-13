import { TreatmentVersionStatus } from '../entities/treatment_version.entity';

export class TreatmentCreatedEvent {
  constructor(
    public readonly treatmentId: number,
    public readonly name: string,
    public readonly createdAt: Date,
  ) {}
}

export class TreatmentVersionCreatedEvent {
  constructor(
    public readonly treatmentId: number,
    public readonly versionId: number,
    public readonly versionNum: number,
    public readonly status: TreatmentVersionStatus,
  ) {}
}

export class TreatmentVersionPublishedEvent {
  constructor(
    public readonly treatmentId: number,
    public readonly versionId: number,
    public readonly versionNum: number,
    public readonly sprayDayS: number,
    public readonly cooldownDayS: number,
    public readonly sprayNightS: number,
    public readonly cooldownNightS: number,
    public readonly publishedAt: Date,
  ) {}
}

export class TreatmentClonedEvent {
  constructor(
    public readonly originalTreatmentId: number,
    public readonly newTreatmentId: number,
    public readonly newName: string,
  ) {}
}

export class TreatmentArchivedEvent {
  constructor(
    public readonly treatmentId: number,
    public readonly archivedAt: Date,
  ) {}
}

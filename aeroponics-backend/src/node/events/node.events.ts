import { NodeHealthStatus } from '../entities/node_registry.entity';

export class NodeTelemetryReceivedEvent {
  constructor(
    public readonly nodeId: number,
    public readonly telemetry: Record<string, any>,
    public readonly timestamp: Date,
  ) {}
}

export class NodeStalenessAlertEvent {
  constructor(
    public readonly nodeId: number,
    public readonly lastSeenAt: Date | null,
    public readonly staleForMs: number,
    public readonly timestamp: Date,
  ) {}
}

export class NodeHealthChangedEvent {
  constructor(
    public readonly nodeId: number,
    public readonly previousStatus: NodeHealthStatus,
    public readonly newStatus: NodeHealthStatus,
    public readonly reason: string,
    public readonly timestamp: Date,
  ) {}
}

export class NodeFaultResetEvent {
  constructor(
    public readonly nodeId: number,
    public readonly resetBy: string,
    public readonly timestamp: Date,
  ) {}
}

export class NodeCalibrationUpdatedEvent {
  constructor(
    public readonly nodeId: number,
    public readonly calibrationId: number,
    public readonly pulsesPerLitre: string,
    public readonly versionNum: number,
    public readonly timestamp: Date,
  ) {}
}

import { PumpAction, PumpCommandOutcome } from '../entities/pump_command.entity';

export class PumpCommandSentEvent {
  constructor(
    public readonly commandId: string,
    public readonly nodeId: number,
    public readonly groupId: number | null,
    public readonly action: PumpAction,
    public readonly rfSeq: number,
    public readonly runLeaseMs: number,
    public readonly sentAt: Date,
  ) {}
}

export class PumpCommandAckedEvent {
  constructor(
    public readonly commandId: string,
    public readonly nodeId: number,
    public readonly outcome: PumpCommandOutcome,
    public readonly ackedAt: Date,
    public readonly latencyMs: number | null,
  ) {}
}

export class PumpCommandFeedbackEvent {
  constructor(
    public readonly commandId: string,
    public readonly nodeId: number,
    public readonly driverFeedback: string,
    public readonly loadFeedback: string,
    public readonly feedbackAt: Date,
  ) {}
}

export class PumpCommandFlowConfirmedEvent {
  constructor(
    public readonly commandId: string,
    public readonly nodeId: number,
    public readonly flowRateLpm: string,
    public readonly confirmedAt: Date,
  ) {}
}

export class PumpCommandFaultEvent {
  constructor(
    public readonly commandId: string,
    public readonly nodeId: number,
    public readonly outcome: PumpCommandOutcome,
    public readonly reason: string,
    public readonly faultedAt: Date,
  ) {}
}

export class CommandAcceptedEvent {
  constructor(
    public readonly nodeId: number,
    public readonly commandId: string,
    public readonly status: string,
    public readonly receivedAt: Date,
  ) {}
}

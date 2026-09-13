import { FlowEvent, FlowFaultCode } from '../entities/flow_event.entity';
import { SensorCalibration } from '../../node/entities/sensor_calibration.entity';

export class FlowEventRecordedEvent {
  constructor(
    public readonly event: FlowEvent,
    public readonly recordedAt: Date = new Date(),
  ) {}
}

export class FlowCalibrationUpdatedEvent {
  constructor(
    public readonly nodeId: number,
    public readonly calibration: SensorCalibration,
    public readonly previousCalibrationId: number | null,
    public readonly calibratedBy: string,
    public readonly updatedAt: Date = new Date(),
  ) {}
}

export class FlowOverRangeAlertEvent {
  constructor(
    public readonly nodeId: number,
    public readonly flowRateLpm: number,
    public readonly thresholdLpm: number = 6.0,
    public readonly faultCode: FlowFaultCode = FlowFaultCode.OVER_RANGE_FAULT,
    public readonly timestamp: Date = new Date(),
  ) {}
}

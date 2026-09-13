import { MeasurementReading } from '../entities/measurement_reading.entity';
import { TuyaMeasurementSession } from '../entities/tuya_measurement_session.entity';

export class MeasurementRecordedEvent {
  constructor(
    public readonly reading: MeasurementReading,
    public readonly session: TuyaMeasurementSession | null,
  ) {}
}

export class MeasurementFailedEvent {
  constructor(
    public readonly sessionId: string,
    public readonly sensorId: string,
    public readonly errorMessage: string,
    public readonly triggeredByUserId?: string | null,
  ) {}
}

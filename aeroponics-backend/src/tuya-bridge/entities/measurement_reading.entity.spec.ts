import {
  MeasurementReading,
  MeasurementTriggerType,
} from './measurement_reading.entity';
import {
  TuyaMeasurementSession,
  TuyaSessionTriggerType,
  TuyaSessionStatus,
} from './tuya_measurement_session.entity';

describe('Measurement Reading & Tuya Session Entities (S3-B6)', () => {
  it('should initialize MeasurementReading with valid ON_DEMAND trigger type', () => {
    const reading = new MeasurementReading();
    reading.time = new Date();
    reading.sensor_id = 'ph-w218-01';
    reading.trigger_type = MeasurementTriggerType.ON_DEMAND;
    reading.ph_value = '6.45';
    reading.ec_value = 1850;
    reading.tds_value = 925;
    reading.temperature_c = '24.5';
    reading.salinity_ppm = 1200;
    reading.orp_mv = 380;
    reading.turbidity_ntu = '1.25';
    reading.triggered_by_user_id = 'usr_operator_01';

    expect(reading.sensor_id).toBe('ph-w218-01');
    expect(reading.trigger_type).toBe(MeasurementTriggerType.ON_DEMAND);
    expect(reading.ph_value).toBe('6.45');
    expect(reading.ec_value).toBe(1850);
  });

  it('HARD RULE S3-TUYA-ON-DEMAND-04: should ONLY allow ON_DEMAND and END_OF_SEASON, reject SCHEDULED', () => {
    const validTypes = Object.values(MeasurementTriggerType);
    expect(validTypes).toHaveLength(2);
    expect(validTypes).toContain('ON_DEMAND');
    expect(validTypes).toContain('END_OF_SEASON');
    expect(validTypes).not.toContain('SCHEDULED');

    const isValidTriggerType = (type: string): boolean => {
      return Object.values(MeasurementTriggerType).includes(
        type as MeasurementTriggerType,
      );
    };

    expect(isValidTriggerType('ON_DEMAND')).toBe(true);
    expect(isValidTriggerType('END_OF_SEASON')).toBe(true);
    expect(isValidTriggerType('SCHEDULED')).toBe(false);
    expect(isValidTriggerType('PERIODIC')).toBe(false);
  });

  it('should initialize TuyaMeasurementSession for on-demand trigger auditing', () => {
    const session = new TuyaMeasurementSession();
    session.session_id = '123e4567-e89b-12d3-a456-426614174000';
    session.sensor_id = 'ph-w218-01';
    session.trigger_type = TuyaSessionTriggerType.ON_DEMAND;
    session.status = TuyaSessionStatus.COMPLETED;
    session.started_at = new Date();
    session.completed_at = new Date();

    expect(session.trigger_type).toBe(TuyaSessionTriggerType.ON_DEMAND);
    expect(session.status).toBe(TuyaSessionStatus.COMPLETED);
    expect(session.completed_at).toBeInstanceOf(Date);
  });
});

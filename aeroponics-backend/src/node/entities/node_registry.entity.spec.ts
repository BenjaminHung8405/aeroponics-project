import {
  NodeRegistry,
  NodeHealthStatus,
  CalibrationStatus,
  ScheduleState,
  OverrideState,
} from './node_registry.entity';
import { SensorCalibration, CalibrationStatusEnum } from './sensor_calibration.entity';

describe('Node Registry & Sensor Calibration Entities (S3-B4)', () => {
  it('should initialize NodeRegistry with default values', () => {
    const node = new NodeRegistry();
    node.node_id = 1;
    node.display_name = 'Node 01';
    node.calibration_status = CalibrationStatus.UNCALIBRATED;
    node.schedule_state = ScheduleState.UNKNOWN;
    node.override_state = OverrideState.NONE;
    node.health_status = NodeHealthStatus.OK;

    expect(node.node_id).toBe(1);
    expect(node.display_name).toBe('Node 01');
    expect(node.calibration_status).toBe(CalibrationStatus.UNCALIBRATED);
    expect(node.health_status).toBe(NodeHealthStatus.OK);
  });

  it('should support SAFE_OFF health status', () => {
    const node = new NodeRegistry();
    node.node_id = 2;
    node.health_status = NodeHealthStatus.SAFE_OFF;

    expect(node.health_status).toBe('SAFE_OFF');
  });

  it('should validate all valid health status values and reject invalid ones', () => {
    const validStatuses = Object.values(NodeHealthStatus);
    expect(validStatuses).toContain('OK');
    expect(validStatuses).toContain('STALE');
    expect(validStatuses).toContain('FAULT');
    expect(validStatuses).toContain('SAFE_OFF');

    const isValidStatus = (status: string): boolean => {
      return Object.values(NodeHealthStatus).includes(status as NodeHealthStatus);
    };

    expect(isValidStatus('OK')).toBe(true);
    expect(isValidStatus('SAFE_OFF')).toBe(true);
    expect(isValidStatus('INVALID_STATUS')).toBe(false);
  });

  it('should store calibration pulses_per_litre with high precision decimal(10,4)', () => {
    const cal = new SensorCalibration();
    cal.id = 1;
    cal.node_id = 1;
    cal.sensor_serial = 'YF-S401-SN-001';
    cal.version_num = 1;
    cal.pulses_per_litre = '5850.1234';
    cal.reference_volume_ml = 1000;
    cal.trial_count = 3;
    cal.mean_pulses = '5850.12';
    cal.variance = '0.0012';
    cal.repeatability_pct = '0.45';
    cal.status = CalibrationStatusEnum.ACTIVE;

    expect(cal.pulses_per_litre).toBe('5850.1234');
    expect(cal.trial_count).toBeGreaterThanOrEqual(3);
    expect(parseFloat(cal.repeatability_pct)).toBeLessThanOrEqual(5.0);
  });
});

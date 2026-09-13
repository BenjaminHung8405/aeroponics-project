import { FlowEvent, FlowFaultCode } from './flow_event.entity';

describe('Flow Event Entity (S3-B6)', () => {
  it('should initialize FlowEvent with normalized parsed fields', () => {
    const event = new FlowEvent();
    event.time = new Date();
    event.season_id = 1;
    event.node_id = 2;
    event.group_id = 1;
    event.litres_total = '12.450';
    event.pulse_count = '72835';
    event.flow_rate_lpm = '2.35';
    event.delivered_volume_ml = 250;
    event.sensor_calibration_id = 1;
    event.flow_confirmed = true;
    event.fault_code = FlowFaultCode.NONE;
    event.is_fault = false;

    expect(event.node_id).toBe(2);
    expect(event.litres_total).toBe('12.450');
    expect(event.flow_rate_lpm).toBe('2.35');
    expect(event.flow_confirmed).toBe(true);
    expect(event.fault_code).toBe(FlowFaultCode.NONE);
  });

  it('HARD RULE S1.5-PARSE-11: should NOT have raw_frame property or column on FlowEvent', () => {
    const event = new FlowEvent();
    // Verify that raw_frame or raw_payload is not a declared property of FlowEvent
    expect('raw_frame' in event).toBe(false);
    expect('raw_payload' in event).toBe(false);

    // Verify prototype keys
    const propertyNames = Object.getOwnPropertyNames(event);
    expect(propertyNames).not.toContain('raw_frame');
  });

  it('should support all FlowFaultCode enum values', () => {
    const faultCodes = Object.values(FlowFaultCode);
    expect(faultCodes).toContain('NONE');
    expect(faultCodes).toContain('NO_FLOW_FAULT');
    expect(faultCodes).toContain('UNEXPECTED_FLOW_FAULT');
    expect(faultCodes).toContain('OVER_RANGE_FAULT');
    expect(faultCodes).toContain('SENSOR_FAULT');
  });
});

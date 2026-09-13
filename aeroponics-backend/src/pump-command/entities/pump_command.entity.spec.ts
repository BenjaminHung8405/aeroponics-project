import {
  PumpCommand,
  PumpAction,
  CommandSource,
  PumpCommandOutcome,
} from './pump_command.entity';
import { PumpStateEvent } from './pump_state_event.entity';
import { PumpFeedbackEvent } from './pump_feedback_event.entity';

describe('Pump Command & Lifecycle Entities (S3-B5)', () => {
  it('should initialize PumpCommand with required fields and defaults', () => {
    const cmd = new PumpCommand();
    cmd.time = new Date();
    cmd.command_id = '550e8400-e29b-41d4-a716-446655440000';
    cmd.season_id = 1;
    cmd.node_id = 2;
    cmd.group_id = 1;
    cmd.action = PumpAction.ON;
    cmd.rf_seq = 101;
    cmd.run_lease_ms = 30000;
    cmd.source = CommandSource.MANUAL_OVERRIDE;
    cmd.outcome = PumpCommandOutcome.PENDING;

    expect(cmd.command_id).toBe('550e8400-e29b-41d4-a716-446655440000');
    expect(cmd.action).toBe(PumpAction.ON);
    expect(cmd.run_lease_ms).toBe(30000);
    expect(cmd.outcome).toBe(PumpCommandOutcome.PENDING);
  });

  it('should cover all 8 outcome states without missing any state', () => {
    const expectedOutcomes = [
      'PENDING',
      'RF_ACKED',
      'FLOW_CONFIRMED',
      'FAULT_NO_ACK',
      'FAULT_NO_FLOW',
      'FAULT_UNEXPECTED_FLOW',
      'FAULT_SENSOR',
      'TIMEOUT',
    ];

    const actualOutcomes = Object.values(PumpCommandOutcome);
    expect(actualOutcomes).toHaveLength(8);
    for (const expected of expectedOutcomes) {
      expect(actualOutcomes).toContain(expected);
    }
  });

  it('should track latency metrics and fault reason on pump command lifecycle completion', () => {
    const cmd = new PumpCommand();
    cmd.time = new Date();
    cmd.command_id = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890';
    cmd.season_id = 1;
    cmd.node_id = 3;
    cmd.action = PumpAction.ON;
    cmd.rf_seq = 42;
    cmd.outcome = PumpCommandOutcome.FAULT_NO_FLOW;
    cmd.command_to_ack_latency_ms = 145;
    cmd.flow_start_latency_ms = 1500;
    cmd.execution_duration_ms = 5000;
    cmd.fault_reason = 'Flow not detected within 1500ms after RF ACK';

    expect(cmd.outcome).toBe(PumpCommandOutcome.FAULT_NO_FLOW);
    expect(cmd.command_to_ack_latency_ms).toBe(145);
    expect(cmd.flow_start_latency_ms).toBe(1500);
    expect(cmd.fault_reason).toContain('Flow not detected');
  });

  it('should initialize PumpStateEvent and PumpFeedbackEvent', () => {
    const stateEvent = new PumpStateEvent();
    stateEvent.time = new Date();
    stateEvent.season_id = 1;
    stateEvent.node_id = 1;
    stateEvent.desired_state = 'ON';
    stateEvent.reported_state = 'ON';
    stateEvent.schedule_state = 'SPRAYING';
    stateEvent.override_state = 'NONE';

    const feedbackEvent = new PumpFeedbackEvent();
    feedbackEvent.time = new Date();
    feedbackEvent.season_id = 1;
    feedbackEvent.node_id = 1;
    feedbackEvent.driver_feedback = 'ON';
    feedbackEvent.load_feedback = 'ON';
    feedbackEvent.driver_feedback_mismatch = false;
    feedbackEvent.current_ma = 850;

    expect(stateEvent.desired_state).toBe('ON');
    expect(feedbackEvent.current_ma).toBe(850);
    expect(feedbackEvent.driver_feedback_mismatch).toBe(false);
  });
});

import 'reflect-metadata';
import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { FlowHistoryQueryDto } from './flow-history-query.dto';
import { UpdateCalibrationDto } from './update-calibration.dto';
import { RecordFlowEventDto } from './record-flow-event.dto';

describe('Flow DTO Validation (S3-G1 / S3-G2)', () => {
  describe('FlowHistoryQueryDto', () => {
    it('should use default hours = 24 and limit = 500 when empty', async () => {
      const dto = plainToInstance(FlowHistoryQueryDto, {});
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.hours).toBe(24);
      expect(dto.limit).toBe(500);
    });

    it('should accept valid hours within [1..720]', async () => {
      for (const h of [1, 12, 24, 48, 168, 720]) {
        const dto = plainToInstance(FlowHistoryQueryDto, { hours: h });
        const errors = await validate(dto);
        expect(errors.length).toBe(0);
        expect(dto.hours).toBe(h);
      }
    });

    it('should REJECT hours <= 0 with validation error', async () => {
      const dto = plainToInstance(FlowHistoryQueryDto, { hours: 0 });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].constraints?.min).toBeDefined();

      const negativeDto = plainToInstance(FlowHistoryQueryDto, { hours: -5 });
      const negErrors = await validate(negativeDto);
      expect(negErrors.length).toBeGreaterThan(0);
    });

    it('should REJECT hours > 720 (e.g. 721 - 30 days maximum limit) with validation error', async () => {
      const dto = plainToInstance(FlowHistoryQueryDto, { hours: 721 });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].constraints?.max).toBeDefined();
    });

    it('should REJECT non-integer hours', async () => {
      const dto = plainToInstance(FlowHistoryQueryDto, { hours: 12.5 });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
    });

    it('should transform numeric string to number', async () => {
      const dto = plainToInstance(FlowHistoryQueryDto, { hours: '48', limit: '100' });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.hours).toBe(48);
      expect(dto.limit).toBe(100);
    });
  });

  describe('UpdateCalibrationDto', () => {
    it('should accept valid pulses_per_litre between 0 and 10000', async () => {
      const dto = plainToInstance(UpdateCalibrationDto, {
        pulses_per_litre: 450.5,
        reference_volume_ml: 1000,
        sensor_serial: 'SEN-01-FLOW',
        calibrated_by: 'Lead Architect',
      });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.getEffectivePulsesPerLitre()).toBe(450.5);
    });

    it('should support calibration_pulses_per_litre alias', async () => {
      const dto = plainToInstance(UpdateCalibrationDto, {
        calibration_pulses_per_litre: 520.1234,
      });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.getEffectivePulsesPerLitre()).toBe(520.1234);
    });

    it('should REJECT pulses_per_litre <= 0', async () => {
      const dtoZero = plainToInstance(UpdateCalibrationDto, {
        pulses_per_litre: 0,
      });
      const errorsZero = await validate(dtoZero);
      expect(errorsZero.length).toBeGreaterThan(0);

      const dtoNeg = plainToInstance(UpdateCalibrationDto, {
        pulses_per_litre: -100,
      });
      const errorsNeg = await validate(dtoNeg);
      expect(errorsNeg.length).toBeGreaterThan(0);
    });

    it('should REJECT pulses_per_litre >= 10000', async () => {
      const dto = plainToInstance(UpdateCalibrationDto, {
        pulses_per_litre: 10000,
      });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
    });
  });

  describe('RecordFlowEventDto', () => {
    it('should accept valid flow event payload for node_id 1..4', async () => {
      const dto = plainToInstance(RecordFlowEventDto, {
        node_id: 2,
        flow_rate_lpm: 2.35,
        litres_total: '10.500',
        pulse_count: '4725',
        delivered_volume_ml: 250,
      });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should REJECT invalid node_id (0, 5)', async () => {
      const dto0 = plainToInstance(RecordFlowEventDto, {
        node_id: 0,
        flow_rate_lpm: 1.5,
      });
      const errors0 = await validate(dto0);
      expect(errors0.length).toBeGreaterThan(0);

      const dto5 = plainToInstance(RecordFlowEventDto, {
        node_id: 5,
        flow_rate_lpm: 1.5,
      });
      const errors5 = await validate(dto5);
      expect(errors5.length).toBeGreaterThan(0);
    });
  });
});

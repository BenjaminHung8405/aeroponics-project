import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { TriggerMeasurementDto } from './trigger-measurement.dto';
import { MeasurementHistoryQueryDto } from './measurement-history-query.dto';
import { MeasurementTriggerType } from '../entities/measurement_reading.entity';

describe('Tuya Bridge DTO Validation (S3-H2)', () => {
  describe('TriggerMeasurementDto', () => {
    it('should validate valid TriggerMeasurementDto with default ON_DEMAND', async () => {
      const dto = plainToInstance(TriggerMeasurementDto, {});
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.trigger_type).toBe(MeasurementTriggerType.ON_DEMAND);
    });

    it('should validate valid END_OF_SEASON trigger type', async () => {
      const dto = plainToInstance(TriggerMeasurementDto, {
        trigger_type: MeasurementTriggerType.END_OF_SEASON,
        notes: 'End of season water quality audit',
      });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should reject invalid trigger_type like SCHEDULED', async () => {
      const dto = plainToInstance(TriggerMeasurementDto, {
        trigger_type: 'SCHEDULED',
      });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('trigger_type');
    });

    it('should reject notes exceeding 500 characters', async () => {
      const dto = plainToInstance(TriggerMeasurementDto, {
        notes: 'a'.repeat(501),
      });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('notes');
    });
  });

  describe('MeasurementHistoryQueryDto', () => {
    it('should validate valid query with default values', async () => {
      const dto = plainToInstance(MeasurementHistoryQueryDto, {});
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.limit).toBe(20);
      expect(dto.offset).toBe(0);
    });

    it('should transform and validate numeric string parameters', async () => {
      const dto = plainToInstance(MeasurementHistoryQueryDto, {
        limit: '50',
        offset: '10',
        trigger_type: 'ON_DEMAND',
      });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.limit).toBe(50);
      expect(dto.offset).toBe(10);
      expect(dto.trigger_type).toBe(MeasurementTriggerType.ON_DEMAND);
    });

    it('should accept limit boundaries [1, 100]', async () => {
      const minDto = plainToInstance(MeasurementHistoryQueryDto, { limit: 1 });
      const maxDto = plainToInstance(MeasurementHistoryQueryDto, { limit: 100 });

      expect(await validate(minDto)).toHaveLength(0);
      expect(await validate(maxDto)).toHaveLength(0);
    });

    it('should reject limit < 1 or limit > 100', async () => {
      const underDto = plainToInstance(MeasurementHistoryQueryDto, { limit: 0 });
      const overDto = plainToInstance(MeasurementHistoryQueryDto, { limit: 101 });

      const underErrors = await validate(underDto);
      const overErrors = await validate(overDto);

      expect(underErrors.length).toBeGreaterThan(0);
      expect(underErrors[0].property).toBe('limit');

      expect(overErrors.length).toBeGreaterThan(0);
      expect(overErrors[0].property).toBe('limit');
    });

    it('should reject negative offset', async () => {
      const dto = plainToInstance(MeasurementHistoryQueryDto, { offset: -1 });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('offset');
    });

    it('HARD RULE: should reject SCHEDULED trigger_type with validation error', async () => {
      const dto = plainToInstance(MeasurementHistoryQueryDto, {
        trigger_type: 'SCHEDULED',
      });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('trigger_type');
    });
  });
});

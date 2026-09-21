import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { SendPumpCommandDto } from './send-pump-command.dto';
import { ListNodeCommandsDto } from './list-node-commands.dto';
import { PumpAction, CommandSource } from '../entities/pump_command.entity';

describe('PumpCommand DTO Validation (S3-F4)', () => {
  describe('SendPumpCommandDto', () => {
    it('should validate valid ON command with default values', async () => {
      const plain = {
        action: 'ON',
      };
      const dto = plainToInstance(SendPumpCommandDto, plain);
      const errors = await validate(dto);
      expect(errors).toHaveLength(0);
      expect(dto.action).toBe(PumpAction.ON);
      expect(dto.run_lease_ms).toBe(30000);
      expect(dto.source).toBe(CommandSource.MANUAL_OVERRIDE);
    });

    it('should validate valid OFF command with custom lease and source', async () => {
      const plain = {
        action: 'OFF',
        node_id: 5,
        override_duration_ms: 60000,
        source: 'FAIL_SAFE',
      };
      const dto = plainToInstance(SendPumpCommandDto, plain);
      const errors = await validate(dto);
      expect(errors).toHaveLength(0);
      expect(dto.action).toBe(PumpAction.OFF);
      expect(dto.node_id).toBe(5);
      expect(dto.override_duration_ms).toBe(60000);
      expect(dto.source).toBe(CommandSource.FAIL_SAFE);
    });

    it('should fail on invalid action', async () => {
      const plain = {
        action: 'INVALID_ACTION',
      };
      const dto = plainToInstance(SendPumpCommandDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('action');
    });

    it('should fail on invalid node_id out of range (0 or > 4)', async () => {
      const plain0 = { action: 'ON', node_id: 0 };
      const dto0 = plainToInstance(SendPumpCommandDto, plain0);
      const errors0 = await validate(dto0);
      expect(errors0.length).toBeGreaterThan(0);

      const plain8 = { action: 'ON', node_id: 8 };
      const dto8 = plainToInstance(SendPumpCommandDto, plain8);
      const errors8 = await validate(dto8);
      expect(errors8.length).toBeGreaterThan(0);
    });

    it('should fail on run_lease_ms out of range (< 1000 or > 300000)', async () => {
      const plainShort = { action: 'ON', run_lease_ms: 500 };
      const dtoShort = plainToInstance(SendPumpCommandDto, plainShort);
      const errorsShort = await validate(dtoShort);
      expect(errorsShort.length).toBeGreaterThan(0);

      const plainLong = { action: 'ON', run_lease_ms: 400000 };
      const dtoLong = plainToInstance(SendPumpCommandDto, plainLong);
      const errorsLong = await validate(dtoLong);
      expect(errorsLong.length).toBeGreaterThan(0);
    });
  });

  describe('ListNodeCommandsDto', () => {
    it('should pass with default values (limit=50, offset=0)', async () => {
      const plain = {};
      const dto = plainToInstance(ListNodeCommandsDto, plain);
      const errors = await validate(dto);
      expect(errors).toHaveLength(0);
      expect(dto.limit).toBe(50);
      expect(dto.offset).toBe(0);
    });

    it('should pass with valid custom limit and offset', async () => {
      const plain = { limit: '100', offset: '20' };
      const dto = plainToInstance(ListNodeCommandsDto, plain);
      const errors = await validate(dto);
      expect(errors).toHaveLength(0);
      expect(dto.limit).toBe(100);
      expect(dto.offset).toBe(20);
    });

    it('should fail on limit < 1', async () => {
      const plain = { limit: 0 };
      const dto = plainToInstance(ListNodeCommandsDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('limit');
    });

    it('should fail on limit > 200', async () => {
      const plain = { limit: 201 };
      const dto = plainToInstance(ListNodeCommandsDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('limit');
    });

    it('should fail on negative offset', async () => {
      const plain = { offset: -1 };
      const dto = plainToInstance(ListNodeCommandsDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('offset');
    });
  });
});

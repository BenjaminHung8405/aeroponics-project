import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { CreateTreatmentDto } from './create-treatment.dto';
import { CreateTreatmentVersionDto } from './create-treatment-version.dto';
import { CloneTreatmentDto } from './clone-treatment.dto';
import { ListTreatmentDto } from './list-treatment.dto';

describe('Treatment DTOs Validation & Transformation', () => {
  describe('CreateTreatmentDto', () => {
    it('should validate successfully with just name', async () => {
      const plain = {
        name: 'Dinh dưỡng Cải Xoăn Giai đoạn 1',
      };
      const dto = plainToInstance(CreateTreatmentDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.name).toBe('Dinh dưỡng Cải Xoăn Giai đoạn 1');
    });

    it('should validate with optional initial version parameters', async () => {
      const plain = {
        name: 'Công thức Xà Lách',
        spray_day_s: 30,
        cooldown_day_s: 300,
        spray_night_s: 15,
        cooldown_night_s: 600,
        created_by: 'Engineer A',
      };
      const dto = plainToInstance(CreateTreatmentDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.spray_day_s).toBe(30);
    });

    it('should trim whitespace from name', async () => {
      const plain = { name: '   Công thức Bạc Hà   ' };
      const dto = plainToInstance(CreateTreatmentDto, plain);
      expect(dto.name).toBe('Công thức Bạc Hà');
    });

    it('should fail when name is empty or missing', async () => {
      const plain = { name: '' };
      const dto = plainToInstance(CreateTreatmentDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('name');
    });

    it('should fail when name exceeds 100 characters', async () => {
      const plain = { name: 'A'.repeat(101) };
      const dto = plainToInstance(CreateTreatmentDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('name');
    });

    it('should fail when optional spray parameter is out of bounds', async () => {
      const plain = {
        name: 'Out of bounds test',
        spray_day_s: 4, // Below minimum of 5
      };
      const dto = plainToInstance(CreateTreatmentDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('spray_day_s');
    });
  });

  describe('CreateTreatmentVersionDto Bounds & Types', () => {
    const validPayload = {
      spray_day_s: 30,
      cooldown_day_s: 300,
      spray_night_s: 15,
      cooldown_night_s: 600,
      created_by: 'Lead Operator',
    };

    it('should pass on valid parameters within industrial bounds', async () => {
      const dto = plainToInstance(CreateTreatmentVersionDto, validPayload);
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should trim created_by', async () => {
      const dto = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        created_by: '   Operator B   ',
      });
      expect(dto.created_by).toBe('Operator B');
    });

    it('should validate exact boundary values [5..300] and [30..7200]', async () => {
      const boundaryMin = plainToInstance(CreateTreatmentVersionDto, {
        spray_day_s: 5,
        cooldown_day_s: 30,
        spray_night_s: 5,
        cooldown_night_s: 30,
      });
      expect(await validate(boundaryMin)).toHaveLength(0);

      const boundaryMax = plainToInstance(CreateTreatmentVersionDto, {
        spray_day_s: 300,
        cooldown_day_s: 7200,
        spray_night_s: 300,
        cooldown_night_s: 7200,
      });
      expect(await validate(boundaryMax)).toHaveLength(0);
    });

    it('should reject spray_day_s < 5 or > 300', async () => {
      const tooLow = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        spray_day_s: 4,
      });
      expect(await validate(tooLow)).toHaveLength(1);

      const tooHigh = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        spray_day_s: 301,
      });
      expect(await validate(tooHigh)).toHaveLength(1);
    });

    it('should reject cooldown_day_s < 30 or > 7200', async () => {
      const tooLow = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        cooldown_day_s: 29,
      });
      expect(await validate(tooLow)).toHaveLength(1);

      const tooHigh = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        cooldown_day_s: 7201,
      });
      expect(await validate(tooHigh)).toHaveLength(1);
    });

    it('should reject spray_night_s < 5 or > 300', async () => {
      const tooLow = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        spray_night_s: 4,
      });
      expect(await validate(tooLow)).toHaveLength(1);

      const tooHigh = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        spray_night_s: 301,
      });
      expect(await validate(tooHigh)).toHaveLength(1);
    });

    it('should reject cooldown_night_s < 30 or > 7200', async () => {
      const tooLow = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        cooldown_night_s: 29,
      });
      expect(await validate(tooLow)).toHaveLength(1);

      const tooHigh = plainToInstance(CreateTreatmentVersionDto, {
        ...validPayload,
        cooldown_night_s: 7201,
      });
      expect(await validate(tooHigh)).toHaveLength(1);
    });
  });

  describe('CloneTreatmentDto', () => {
    it('should validate with empty object', async () => {
      const dto = plainToInstance(CloneTreatmentDto, {});
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should trim custom name when provided', async () => {
      const dto = plainToInstance(CloneTreatmentDto, {
        name: '   New Cloned Formula   ',
      });
      expect(dto.name).toBe('New Cloned Formula');
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should fail when custom name exceeds 100 characters', async () => {
      const dto = plainToInstance(CloneTreatmentDto, {
        name: 'X'.repeat(101),
      });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('name');
    });
  });

  describe('ListTreatmentDto', () => {
    it('should validate with defaults', async () => {
      const dto = plainToInstance(ListTreatmentDto, {});
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should parse boolean string for is_archived', async () => {
      const dtoTrue = plainToInstance(ListTreatmentDto, { is_archived: 'true' });
      expect(dtoTrue.is_archived).toBe(true);

      const dtoFalse = plainToInstance(ListTreatmentDto, { is_archived: 'false' });
      expect(dtoFalse.is_archived).toBe(false);
    });

    it('should fail when limit < 1 or limit > 100', async () => {
      const low = plainToInstance(ListTreatmentDto, { limit: 0 });
      expect(await validate(low)).toHaveLength(1);

      const high = plainToInstance(ListTreatmentDto, { limit: 101 });
      expect(await validate(high)).toHaveLength(1);
    });

    it('should fail when offset < 0', async () => {
      const neg = plainToInstance(ListTreatmentDto, { offset: -1 });
      expect(await validate(neg)).toHaveLength(1);
    });
  });
});

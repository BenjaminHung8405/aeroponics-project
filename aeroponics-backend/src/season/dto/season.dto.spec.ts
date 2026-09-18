import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { CreateSeasonDto } from './create-season.dto';
import { EndSeasonDto } from './end-season.dto';
import { ListSeasonDto } from './list-season.dto';
import { SeasonStatus } from '../entities/season.entity';

describe('Season DTOs Validation & Transformation', () => {
  describe('CreateSeasonDto', () => {
    it('should validate successfully with valid payload', async () => {
      const plain = {
        name: 'Vụ Cải Xoăn Thủy Canh 2026',
        notes: 'Thử nghiệm hệ thống 4 node MEGA8',
      };
      const dto = plainToInstance(CreateSeasonDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.name).toBe('Vụ Cải Xoăn Thủy Canh 2026');
    });

    it('should trim whitespace from name and notes', async () => {
      const plain = {
        name: '   Vụ Mùa Có Whitespace   ',
        notes: '   Ghi chú có khoảng trắng   ',
      };
      const dto = plainToInstance(CreateSeasonDto, plain);
      expect(dto.name).toBe('Vụ Mùa Có Whitespace');
      expect(dto.notes).toBe('Ghi chú có khoảng trắng');
    });

    it('should fail when name is empty or missing', async () => {
      const plain = { name: '' };
      const dto = plainToInstance(CreateSeasonDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('name');
    });

    it('should fail when name exceeds 100 characters', async () => {
      const plain = { name: 'A'.repeat(101) };
      const dto = plainToInstance(CreateSeasonDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('name');
      expect(errors[0].constraints?.maxLength).toBeDefined();
    });

    it('should fail when notes exceed 1000 characters', async () => {
      const plain = {
        name: 'Vụ Hợp Lệ',
        notes: 'X'.repeat(1001),
      };
      const dto = plainToInstance(CreateSeasonDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('notes');
      expect(errors[0].constraints?.maxLength).toBeDefined();
    });

    it('should validate successfully with target_ec and target_ph', async () => {
      const plain = {
        name: 'Vụ Thử Nghiệm EC pH',
        target_ec: 1.6,
        target_ph: 6.0,
      };
      const dto = plainToInstance(CreateSeasonDto, plain);
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.target_ec).toBe(1.6);
      expect(dto.target_ph).toBe(6.0);
    });

    it('should fail when target_ec is negative or exceeds maximum', async () => {
      const plainNegative = {
        name: 'Vụ Lỗi EC Âm',
        target_ec: -1.0,
      };
      const dtoNegative = plainToInstance(CreateSeasonDto, plainNegative);
      const errorsNegative = await validate(dtoNegative);
      expect(errorsNegative.length).toBeGreaterThan(0);
      expect(errorsNegative[0].property).toBe('target_ec');
      expect(errorsNegative[0].constraints?.min).toBeDefined();

      const plainExceed = {
        name: 'Vụ Lỗi EC Quá Lớn',
        target_ec: 15.0,
      };
      const dtoExceed = plainToInstance(CreateSeasonDto, plainExceed);
      const errorsExceed = await validate(dtoExceed);
      expect(errorsExceed.length).toBeGreaterThan(0);
      expect(errorsExceed[0].property).toBe('target_ec');
      expect(errorsExceed[0].constraints?.max).toBeDefined();
    });

    it('should fail when target_ph is negative or exceeds 14', async () => {
      const plainNegative = {
        name: 'Vụ Lỗi pH Âm',
        target_ph: -0.5,
      };
      const dtoNegative = plainToInstance(CreateSeasonDto, plainNegative);
      const errorsNegative = await validate(dtoNegative);
      expect(errorsNegative.length).toBeGreaterThan(0);
      expect(errorsNegative[0].property).toBe('target_ph');
      expect(errorsNegative[0].constraints?.min).toBeDefined();

      const plainExceed = {
        name: 'Vụ Lỗi pH > 14',
        target_ph: 14.5,
      };
      const dtoExceed = plainToInstance(CreateSeasonDto, plainExceed);
      const errorsExceed = await validate(dtoExceed);
      expect(errorsExceed.length).toBeGreaterThan(0);
      expect(errorsExceed[0].property).toBe('target_ph');
      expect(errorsExceed[0].constraints?.max).toBeDefined();
    });
  });

  describe('EndSeasonDto', () => {
    it('should validate with empty body', async () => {
      const dto = plainToInstance(EndSeasonDto, {});
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should validate with valid notes', async () => {
      const dto = plainToInstance(EndSeasonDto, {
        notes: 'Thu hoạch đạt 150kg cải kale',
      });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should fail when notes exceed 1000 characters', async () => {
      const dto = plainToInstance(EndSeasonDto, {
        notes: 'Z'.repeat(1001),
      });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('notes');
    });
  });

  describe('ListSeasonDto', () => {
    it('should validate with default values when empty', async () => {
      const dto = plainToInstance(ListSeasonDto, {});
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
    });

    it('should validate with valid SeasonStatus enum', async () => {
      const dto = plainToInstance(ListSeasonDto, {
        status: SeasonStatus.ACTIVE,
        limit: 15,
        offset: 5,
      });
      const errors = await validate(dto);
      expect(errors.length).toBe(0);
      expect(dto.status).toBe(SeasonStatus.ACTIVE);
      expect(dto.limit).toBe(15);
      expect(dto.offset).toBe(5);
    });

    it('should fail with invalid SeasonStatus enum value', async () => {
      const dto = plainToInstance(ListSeasonDto, {
        status: 'NON_EXISTENT_STATUS' as any,
      });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
      expect(errors[0].property).toBe('status');
      expect(errors[0].constraints?.isEnum).toBeDefined();
    });

    it('should fail when limit < 1 or limit > 100', async () => {
      const dtoBelow = plainToInstance(ListSeasonDto, { limit: 0 });
      const errorsBelow = await validate(dtoBelow);
      expect(errorsBelow.length).toBeGreaterThan(0);

      const dtoAbove = plainToInstance(ListSeasonDto, { limit: 101 });
      const errorsAbove = await validate(dtoAbove);
      expect(errorsAbove.length).toBeGreaterThan(0);
    });

    it('should fail when offset < 0', async () => {
      const dto = plainToInstance(ListSeasonDto, { offset: -1 });
      const errors = await validate(dto);
      expect(errors.length).toBeGreaterThan(0);
    });
  });
});

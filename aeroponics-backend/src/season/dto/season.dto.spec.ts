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

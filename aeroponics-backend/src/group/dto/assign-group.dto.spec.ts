import 'reflect-metadata';
import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { AssignGroupDto } from './assign-group.dto';

describe('AssignGroupDto Validation', () => {
  it('should pass with valid treatment_version_id and node_ids', async () => {
    const dto = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [4, 5, 6],
    });
    const errors = await validate(dto);
    expect(errors.length).toBe(0);
  });

  it('should reject non-integer or negative treatment_version_id', async () => {
    const dtoNegative = plainToInstance(AssignGroupDto, {
      treatment_version_id: 0,
      node_ids: [4],
    });
    const errorsNegative = await validate(dtoNegative);
    expect(errorsNegative.length).toBeGreaterThan(0);
    expect(errorsNegative[0].property).toBe('treatment_version_id');

    const dtoFloat = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1.5,
      node_ids: [4],
    });
    const errorsFloat = await validate(dtoFloat);
    expect(errorsFloat.length).toBeGreaterThan(0);
  });

  it('should reject empty node_ids array', async () => {
    const dto = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [],
    });
    const errors = await validate(dto);
    expect(errors.length).toBeGreaterThan(0);
    expect(errors[0].property).toBe('node_ids');
  });

  it('should reject node_ids containing duplicates', async () => {
    const dto = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [4, 5, 5],
    });
    const errors = await validate(dto);
    expect(errors.length).toBeGreaterThan(0);
    expect(errors[0].property).toBe('node_ids');
  });

   it('should reject node_ids with out of range values (< 1 or > 15)', async () => {
    const dtoZero = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [0, 4],
    });
    const errorsZero = await validate(dtoZero);
    expect(errorsZero.length).toBeGreaterThan(0);

    const dtoEight = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
       node_ids: [16],
    });
    const errorsEight = await validate(dtoEight);
    expect(errorsEight.length).toBeGreaterThan(0);
  });

   it('should reject duplicate node_ids', async () => {
    const dto = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [4, 5, 6, 7, 5],
    });
    const errors = await validate(dto);
    expect(errors.length).toBeGreaterThan(0);
  });
});

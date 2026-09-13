import 'reflect-metadata';
import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { AssignGroupDto } from './assign-group.dto';

describe('AssignGroupDto Validation', () => {
  it('should pass with valid treatment_version_id and node_ids', async () => {
    const dto = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [1, 2, 3],
    });
    const errors = await validate(dto);
    expect(errors.length).toBe(0);
  });

  it('should reject non-integer or negative treatment_version_id', async () => {
    const dtoNegative = plainToInstance(AssignGroupDto, {
      treatment_version_id: 0,
      node_ids: [1],
    });
    const errorsNegative = await validate(dtoNegative);
    expect(errorsNegative.length).toBeGreaterThan(0);
    expect(errorsNegative[0].property).toBe('treatment_version_id');

    const dtoFloat = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1.5,
      node_ids: [1],
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
      node_ids: [1, 2, 2],
    });
    const errors = await validate(dto);
    expect(errors.length).toBeGreaterThan(0);
    expect(errors[0].property).toBe('node_ids');
  });

  it('should reject node_ids with out of range values (< 1 or > 4)', async () => {
    const dtoZero = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [0, 1],
    });
    const errorsZero = await validate(dtoZero);
    expect(errorsZero.length).toBeGreaterThan(0);

    const dtoFive = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [5],
    });
    const errorsFive = await validate(dtoFive);
    expect(errorsFive.length).toBeGreaterThan(0);
  });

  it('should reject node_ids with more than 4 items', async () => {
    const dto = plainToInstance(AssignGroupDto, {
      treatment_version_id: 1,
      node_ids: [1, 2, 3, 4, 5],
    });
    const errors = await validate(dto);
    expect(errors.length).toBeGreaterThan(0);
  });
});

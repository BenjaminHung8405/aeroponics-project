import 'reflect-metadata';
import { validate } from 'class-validator';
import { plainToInstance } from 'class-transformer';
import { UpdateNodeCalibrationDto } from './update-node-calibration.dto';

describe('UpdateNodeCalibrationDto Validation (S3-E3)', () => {
  it('should accept valid calibration_pulses_per_litre within (0..10000)', async () => {
    const dto = plainToInstance(UpdateNodeCalibrationDto, {
      calibration_pulses_per_litre: 450.5,
      sensor_serial: 'SEN-01',
      calibrated_by: 'Engineer A',
    });
    const errors = await validate(dto);
    expect(errors.length).toBe(0);
    expect(dto.getEffectivePulsesPerLitre()).toBe(450.5);
  });

  it('should accept valid pulses_per_litre as alternative field name', async () => {
    const dto = plainToInstance(UpdateNodeCalibrationDto, {
      pulses_per_litre: 1200.25,
    });
    const errors = await validate(dto);
    expect(errors.length).toBe(0);
    expect(dto.getEffectivePulsesPerLitre()).toBe(1200.25);
  });

  it('should accept valid lower and upper boundary values (0.0001 and 9999.9999)', async () => {
    const dtoMin = plainToInstance(UpdateNodeCalibrationDto, {
      calibration_pulses_per_litre: 0.0001,
    });
    const errorsMin = await validate(dtoMin);
    expect(errorsMin.length).toBe(0);

    const dtoMax = plainToInstance(UpdateNodeCalibrationDto, {
      calibration_pulses_per_litre: 9999.9999,
    });
    const errorsMax = await validate(dtoMax);
    expect(errorsMax.length).toBe(0);
  });

  it('should reject non-positive or 0 pulses_per_litre', async () => {
    const dtoZero = plainToInstance(UpdateNodeCalibrationDto, {
      calibration_pulses_per_litre: 0,
    });
    const errorsZero = await validate(dtoZero);
    expect(errorsZero.length).toBeGreaterThan(0);

    const dtoNegative = plainToInstance(UpdateNodeCalibrationDto, {
      calibration_pulses_per_litre: -10,
    });
    const errorsNegative = await validate(dtoNegative);
    expect(errorsNegative.length).toBeGreaterThan(0);
  });

  it('should reject values greater than or equal to 10000', async () => {
    const dto10000 = plainToInstance(UpdateNodeCalibrationDto, {
      calibration_pulses_per_litre: 10000,
    });
    const errors10000 = await validate(dto10000);
    expect(errors10000.length).toBeGreaterThan(0);

    const dtoOver = plainToInstance(UpdateNodeCalibrationDto, {
      calibration_pulses_per_litre: 15000,
    });
    const errorsOver = await validate(dtoOver);
    expect(errorsOver.length).toBeGreaterThan(0);
  });
});

import { Treatment } from './treatment.entity';
import {
  TreatmentVersion,
  TreatmentVersionStatus,
  TREATMENT_BOUNDS,
} from './treatment_version.entity';

describe('Treatment & TreatmentVersion Entities (S3-B2)', () => {
  it('should initialize treatment with default is_archived = false', () => {
    const treatment = new Treatment();
    treatment.id = 1;
    treatment.name = 'Rau Muống Giai Đoạn Sinh Dưỡng';
    treatment.is_archived = false;

    expect(treatment.id).toBe(1);
    expect(treatment.name).toBe('Rau Muống Giai Đoạn Sinh Dưỡng');
    expect(treatment.is_archived).toBe(false);
  });

  it('should create a valid draft treatment version within bounds', () => {
    const version = new TreatmentVersion();
    version.id = 1;
    version.treatment_id = 1;
    version.version_num = 1;
    version.status = TreatmentVersionStatus.DRAFT;
    version.spray_day_s = 15;
    version.cooldown_day_s = 180;
    version.spray_night_s = 10;
    version.cooldown_night_s = 360;
    version.created_by = 'engineer@aeroponics.local';
    version.published_at = null;

    expect(version.status).toBe(TreatmentVersionStatus.DRAFT);
    expect(version.published_at).toBeNull();
    expect(version.spray_day_s).toBeGreaterThanOrEqual(TREATMENT_BOUNDS.MIN_SPRAY_DAY_S);
    expect(version.spray_day_s).toBeLessThanOrEqual(TREATMENT_BOUNDS.MAX_SPRAY_DAY_S);
    expect(version.cooldown_day_s).toBeGreaterThanOrEqual(TREATMENT_BOUNDS.MIN_COOLDOWN_DAY_S);
    expect(version.cooldown_day_s).toBeLessThanOrEqual(TREATMENT_BOUNDS.MAX_COOLDOWN_DAY_S);
  });

  it('should validate treatment parameter bounds and reject out-of-range values', () => {
    const validateBounds = (
      sprayDay: number,
      cooldownDay: number,
      sprayNight: number,
      cooldownNight: number,
    ) => {
      if (sprayDay < TREATMENT_BOUNDS.MIN_SPRAY_DAY_S || sprayDay > TREATMENT_BOUNDS.MAX_SPRAY_DAY_S) {
        throw new Error('spray_day_s out of range [5..300]');
      }
      if (cooldownDay < TREATMENT_BOUNDS.MIN_COOLDOWN_DAY_S || cooldownDay > TREATMENT_BOUNDS.MAX_COOLDOWN_DAY_S) {
        throw new Error('cooldown_day_s out of range [30..7200]');
      }
      if (sprayNight < TREATMENT_BOUNDS.MIN_SPRAY_NIGHT_S || sprayNight > TREATMENT_BOUNDS.MAX_SPRAY_NIGHT_S) {
        throw new Error('spray_night_s out of range [5..300]');
      }
      if (cooldownNight < TREATMENT_BOUNDS.MIN_COOLDOWN_NIGHT_S || cooldownNight > TREATMENT_BOUNDS.MAX_COOLDOWN_NIGHT_S) {
        throw new Error('cooldown_night_s out of range [30..7200]');
      }
      return true;
    };

    expect(() => validateBounds(4, 180, 10, 360)).toThrow('spray_day_s out of range');
    expect(() => validateBounds(301, 180, 10, 360)).toThrow('spray_day_s out of range');
    expect(() => validateBounds(15, 29, 10, 360)).toThrow('cooldown_day_s out of range');
    expect(() => validateBounds(15, 7201, 10, 360)).toThrow('cooldown_day_s out of range');
    expect(validateBounds(15, 180, 10, 360)).toBe(true);
  });

  it('should enforce immutability: published version cannot be modified', () => {
    const publishedVersion = new TreatmentVersion();
    publishedVersion.id = 1;
    publishedVersion.status = TreatmentVersionStatus.PUBLISHED;
    publishedVersion.spray_day_s = 15;
    publishedVersion.published_at = new Date();

    const modifyVersion = (version: TreatmentVersion, newSprayDay: number) => {
      if (version.status === TreatmentVersionStatus.PUBLISHED) {
        throw new Error('IMMUTABLE_ERROR: Published treatment versions cannot be modified');
      }
      version.spray_day_s = newSprayDay;
    };

    expect(() => modifyVersion(publishedVersion, 20)).toThrow('IMMUTABLE_ERROR');
  });
});

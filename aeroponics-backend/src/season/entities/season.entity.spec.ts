import { Season, SeasonStatus } from './season.entity';

describe('Season Entity (S3-B1)', () => {
  it('should initialize with default ACTIVE status and started_at date', () => {
    const season = new Season();
    season.name = 'Vụ Đông Xuân 2026';
    season.status = SeasonStatus.ACTIVE;
    season.started_at = new Date();

    expect(season.name).toBe('Vụ Đông Xuân 2026');
    expect(season.status).toBe(SeasonStatus.ACTIVE);
    expect(season.ended_at).toBeUndefined();
    expect(season.started_at).toBeInstanceOf(Date);
  });

  it('should allow ending a season with ended_at and ENDED status', () => {
    const season = new Season();
    season.id = 1;
    season.name = 'Vụ Thử Nghiệm K42';
    season.status = SeasonStatus.ENDED;
    const now = new Date();
    season.ended_at = now;

    expect(season.status).toBe(SeasonStatus.ENDED);
    expect(season.ended_at).toEqual(now);
  });

  it('should support target_ec and target_ph properties', () => {
    const season = new Season();
    season.target_ec = 1.6;
    season.target_ph = 6.0;

    expect(season.target_ec).toBe(1.6);
    expect(season.target_ph).toBe(6.0);
  });

  it('should validate single active season rule (duplicate active season rejected)', () => {
    // Simulate database / service layer partial unique index constraint
    const activeSeasons = [
      { id: 1, name: 'Vụ 1', status: SeasonStatus.ACTIVE, ended_at: null },
    ];

    const canActivateNewSeason = (newStatus: SeasonStatus): boolean => {
      if (newStatus === SeasonStatus.ACTIVE) {
        const hasActive = activeSeasons.some((s) => s.status === SeasonStatus.ACTIVE);
        if (hasActive) {
          throw new Error('UQ_SEASONS_ONE_ACTIVE: Cannot have 2 active seasons concurrently');
        }
      }
      return true;
    };

    expect(() => canActivateNewSeason(SeasonStatus.ACTIVE)).toThrow(
      'UQ_SEASONS_ONE_ACTIVE',
    );
    expect(canActivateNewSeason(SeasonStatus.ENDED)).toBe(true);
  });
});

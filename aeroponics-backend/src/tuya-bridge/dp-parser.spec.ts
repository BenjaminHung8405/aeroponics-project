import { parseDps } from './dp-parser';

describe('Tuya DP Parser (S3-H1)', () => {
  it('should parse full standard Tuya PH-W218 DPS payload correctly', () => {
    const dps = {
      '101': 685, // pH 6.85
      '102': 1850, // EC 1850 µS/cm
      '103': 925, // TDS 925 ppm
      '104': 245, // Temp 24.5 °C
      '105': 1200, // Salinity 1200 ppm
      '106': 380, // ORP 380 mV
      '107': 125, // Turbidity 1.25 NTU (125 / 100 or raw)
      '108': 95, // Battery 95%
    };

    const result = parseDps(dps);

    expect(result.ph_value).toBe('6.85');
    expect(result.ec_value).toBe(1850);
    expect(result.tds_value).toBe(925);
    expect(result.temperature_c).toBe('24.5');
    expect(result.salinity_ppm).toBe(1200);
    expect(result.orp_mv).toBe(380);
    expect(result.turbidity_ntu).toBe('12.50');
    expect(result.battery_pct).toBe(95);
  });

  it('HARD RULE S3-H1: should handle empty or null DPS gracefully with null fields (never throw)', () => {
    expect(() => parseDps({})).not.toThrow();
    expect(parseDps({})).toEqual({
      ph_value: null,
      ec_value: null,
      tds_value: null,
      temperature_c: null,
      salinity_ppm: null,
      orp_mv: null,
      turbidity_ntu: null,
      battery_pct: null,
    });

    expect(() => parseDps(null)).not.toThrow();
    expect(parseDps(null)).toEqual({
      ph_value: null,
      ec_value: null,
      tds_value: null,
      temperature_c: null,
      salinity_ppm: null,
      orp_mv: null,
      turbidity_ntu: null,
      battery_pct: null,
    });

    expect(() => parseDps(undefined)).not.toThrow();
    expect(parseDps(undefined).ph_value).toBeNull();
  });

  it('should parse partial DPS payload gracefully setting missing fields to null', () => {
    const partialDps = {
      '101': 700,
      '104': 250,
    };

    const result = parseDps(partialDps);
    expect(result.ph_value).toBe('7.00');
    expect(result.temperature_c).toBe('25.0');
    expect(result.ec_value).toBeNull();
    expect(result.tds_value).toBeNull();
    expect(result.salinity_ppm).toBeNull();
    expect(result.orp_mv).toBeNull();
    expect(result.turbidity_ntu).toBeNull();
    expect(result.battery_pct).toBeNull();
  });

  it('should support alternative fallback DP indices (1, 2, 8, 15)', () => {
    const altDps = {
      '1': 68, // pH (68 / 10 = 6.80)
      '2': 450, // TDS
      '8': 225, // Temp (22.5 °C)
      '15': 88, // Battery
    };

    const result = parseDps(altDps);
    expect(result.ph_value).toBe('6.80');
    expect(result.tds_value).toBe(450);
    expect(result.temperature_c).toBe('22.5');
    expect(result.battery_pct).toBe(88);
  });

  it('should handle decimal numbers and strings directly', () => {
    const directDps = {
      '101': '6.45',
      '104': '23.8',
      '107': '1.20',
    };

    const result = parseDps(directDps);
    expect(result.ph_value).toBe('6.45');
    expect(result.temperature_c).toBe('23.8');
    expect(result.turbidity_ntu).toBe('1.20');
  });

  it('should clamp battery percentage between 0 and 100', () => {
    expect(parseDps({ '108': 150 }).battery_pct).toBe(100);
    expect(parseDps({ '108': -20 }).battery_pct).toBe(0);
    expect(parseDps({ '108': 75.4 }).battery_pct).toBe(75);
  });
});

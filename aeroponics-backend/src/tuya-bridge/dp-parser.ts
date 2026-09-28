import { Logger } from '@nestjs/common';

const logger = new Logger('DpParser');

export interface ParsedDpsResult {
  ph_value: string | null;
  ec_value: number | null;
  tds_value: number | null;
  temperature_c: string | null;
  salinity_ppm: number | null;
  orp_mv: number | null;
  turbidity_ntu: string | null;
  battery_pct: number | null;
}

/**
 * Parses raw Tuya DPS key-value pairs into structured water quality parameters.
 * Gracefully handles index misses (missing DPs return null instead of throwing).
 */
export function parseDps(
  dps: Record<string, any> | undefined | null,
): ParsedDpsResult {
  if (!dps || typeof dps !== 'object') {
    return {
      ph_value: null,
      ec_value: null,
      tds_value: null,
      temperature_c: null,
      salinity_ppm: null,
      orp_mv: null,
      turbidity_ntu: null,
      battery_pct: null,
    };
  }

  // 1. pH Value: If DP 101/1 is present, use it; otherwise use DP 106 (standard PH-W218)
  const legacyPh = dps['101'] ?? dps['1'] ?? dps[101] ?? dps[1];
  const standardPh = dps['106'] ?? dps[106];
  const rawPh = legacyPh !== undefined ? legacyPh : standardPh;
  const ph_value = parsePhValue(rawPh);

  // 2. EC Value: DP 116 (standard PH-W218), fallback DP 102
  const rawEc = dps['116'] ?? dps[116] ?? dps['102'] ?? dps[102];
  const ec_value = parseInteger(rawEc, 'EC');

  // 3. TDS Value: DP 111 (standard PH-W218), fallback DP 103 or DP 2
  const rawTds = dps['111'] ?? dps[111] ?? dps['103'] ?? dps['2'] ?? dps[103] ?? dps[2];
  const tds_value = parseInteger(rawTds, 'TDS');

  // 4. Temperature °C: DP 8 (standard PH-W218), fallback DP 104
  const rawTemp = dps['8'] ?? dps[8] ?? dps['104'] ?? dps[104];
  const temperature_c = parseTemperature(rawTemp);

  // 5. Salinity ppm: DP 121 (standard PH-W218), fallback DP 105
  const rawSalinity = dps['121'] ?? dps[121] ?? dps['105'] ?? dps[105];
  const salinity_ppm = parseInteger(rawSalinity, 'Salinity');

  // 6. ORP mV: DP 131 (standard PH-W218), or DP 106 if DP 101/1 was used as pH
  const rawOrp = dps['131'] ?? dps[131] ?? (legacyPh !== undefined ? standardPh : null);
  const orp_mv = parseInteger(rawOrp, 'ORP');

  // 7. Turbidity NTU: DP 107
  const rawTurbidity = dps['107'] ?? dps[107];
  const turbidity_ntu = parseTurbidity(rawTurbidity);

  // 8. Battery %: DP 108, 109, or 15
  const rawBattery =
    dps['108'] ?? dps['109'] ?? dps['15'] ?? dps[108] ?? dps[109] ?? dps[15];
  const battery_pct = parseBattery(rawBattery);

  return {
    ph_value,
    ec_value,
    tds_value,
    temperature_c,
    salinity_ppm,
    orp_mv,
    turbidity_ntu,
    battery_pct,
  };
}

function parsePhValue(raw: unknown): string | null {
  if (raw === undefined || raw === null || raw === '') return null;
  const num = Number(raw);
  if (isNaN(num) || num === 1500) return null;

  let scaled = num;
  if (num > 140) {
    scaled = num / 100;
  } else if (num > 14) {
    scaled = num / 10;
  }

  if (scaled < 0 || scaled > 14) {
    logger.warn(`Parsed pH value ${scaled} is out of realistic range [0, 14]`);
  }

  return scaled.toFixed(2);
}

function parseTemperature(raw: unknown): string | null {
  if (raw === undefined || raw === null || raw === '') return null;
  const num = Number(raw);
  if (isNaN(num)) return null;

  let scaled = num;
  if (Math.abs(num) >= 100) {
    scaled = num / 10;
  }

  if (scaled < -10 || scaled > 60) {
    logger.warn(
      `Parsed temperature ${scaled}°C is out of realistic range [-10, 60]`,
    );
  }

  return scaled.toFixed(1);
}

function parseTurbidity(raw: unknown): string | null {
  if (raw === undefined || raw === null || raw === '') return null;
  const num = Number(raw);
  if (isNaN(num)) return null;

  let scaled = num;
  if (num > 100) {
    scaled = num / 10;
  }

  return scaled.toFixed(2);
}

function parseInteger(raw: unknown, label: string): number | null {
  if (raw === undefined || raw === null || raw === '') return null;
  const num = Number(raw);
  if (isNaN(num)) return null;
  const rounded = Math.round(num);

  if (rounded < 0) {
    logger.warn(`Parsed ${label} ${rounded} is negative`);
  }

  return rounded;
}

function parseBattery(raw: unknown): number | null {
  if (raw === undefined || raw === null || raw === '') return null;
  const num = Number(raw);
  if (isNaN(num)) return null;
  return Math.min(100, Math.max(0, Math.round(num)));
}

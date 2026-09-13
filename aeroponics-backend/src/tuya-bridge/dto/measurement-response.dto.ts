import { MeasurementTriggerType } from '../entities/measurement_reading.entity';

export interface MeasurementReadingResponse {
  time: Date;
  session_id: string | null;
  sensor_id: string;
  trigger_type: MeasurementTriggerType;
  ph_value: string | null;
  ec_value: number | null;
  tds_value: number | null;
  temperature_c: string | null;
  salinity_ppm: number | null;
  orp_mv: number | null;
  turbidity_ntu: string | null;
  battery_pct: number | null;
  calibrated_at: Date | null;
  triggered_by_user_id: string | null;
}

export interface MeasurementHistoryResponse {
  readings: MeasurementReadingResponse[];
  total: number;
  limit: number;
  offset: number;
}

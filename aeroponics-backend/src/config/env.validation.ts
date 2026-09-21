import { plainToInstance, Transform } from 'class-transformer';
import { IsBoolean, IsEnum, IsNumber, IsOptional, IsString, validateSync } from 'class-validator';

export enum Environment {
  Development = 'development',
  Production = 'production',
  Test = 'test',
}

export class EnvironmentVariables {
  @IsEnum(Environment)
  @IsOptional()
  NODE_ENV: Environment = Environment.Development;

  @IsNumber()
  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  PORT: number = 3001;

  @IsString()
  @IsOptional()
  DATABASE_URL?: string;

  @IsString()
  @IsOptional()
  DB_HOST?: string;

  @IsNumber()
  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  DB_PORT?: number = 5432;

  @IsString()
  @IsOptional()
  DB_USER?: string;

  @IsString()
  @IsOptional()
  DB_PASS?: string;

  @IsString()
  @IsOptional()
  DB_NAME?: string;

  @IsString()
  @IsOptional()
  MQTT_HOST?: string;

  @IsNumber()
  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  MQTT_PORT?: number = 1883;

  @IsString()
  @IsOptional()
  MQTT_USERNAME?: string;

  @IsString()
  @IsOptional()
  MQTT_PASSWORD?: string;

  @IsString()
  @IsOptional()
  MQTT_CLIENT_ID: string = 'aeroponics_backend_service';

  @IsNumber()
  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  MQTT_ANTIREPLAY_WINDOW_MS: number = 60000;

  @IsNumber()
  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  STALE_THRESHOLD_MS: number = 120000;

  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  RF_SCAN_RESULT_TIMEOUT_MS: number = 10000;

  @IsString()
  @IsOptional()
  JWT_SECRET: string = 'aeroponics_super_secret_jwt_key_default_32chars';

  @IsString()
  @IsOptional()
  ADMIN_USERNAME: string = 'admin';

  @IsString()
  @IsOptional()
  ADMIN_PASSWORD?: string = '123456';

  @IsString()
  @IsOptional()
  TUYA_DEVICE_IP?: string;

  @IsBoolean()
  @IsOptional()
  @Transform(({ value }) => value === 'true' || value === true)
  TUYA_BRIDGE_ENABLED: boolean = false;

  @IsString()
  @IsOptional()
  TUYA_DEVICE_ID?: string;

  @IsString()
  @IsOptional()
  TUYA_LOCAL_KEY?: string;

  @IsString()
  @IsOptional()
  TUYA_SENSOR_ID: string = 'ph-w218-01';

  @IsNumber()
  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  TUYA_ON_DEMAND_TIMEOUT_MS: number = 5000;

  @IsNumber()
  @IsOptional()
  @Transform(({ value }) => parseInt(value, 10))
  TUYA_COOLDOWN_WINDOW_MS: number = 60000;
}

export function validate(config: Record<string, unknown>) {
  const validatedConfig = plainToInstance(EnvironmentVariables, config, {
    enableImplicitConversion: true,
  });
  const errors = validateSync(validatedConfig, {
    skipMissingProperties: false,
  });

  if (errors.length > 0) {
    throw new Error(`Environment validation error: ${errors.toString()}`);
  }

  // Industrial IoT safety requirement:
  // In production or when explicitly configured, DATABASE_URL or DB_HOST must be provided.
  if (
    validatedConfig.NODE_ENV === Environment.Production &&
    !validatedConfig.DATABASE_URL &&
    !validatedConfig.DB_HOST
  ) {
    throw new Error(
      'Configurable environment error: Missing DATABASE_URL (or DB_HOST) in production mode.',
    );
  }

  return validatedConfig;
}

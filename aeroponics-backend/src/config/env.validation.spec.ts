import 'reflect-metadata';
import { Environment, validate } from './env.validation';

describe('Environment Validation', () => {
  it('should validate valid environment configuration successfully', () => {
    const config = {
      NODE_ENV: 'development',
      PORT: '3001',
      DATABASE_URL: 'postgresql://user:pass@localhost:5432/aeroponics',
      MQTT_HOST: 'localhost',
      MQTT_PORT: '1883',
      JWT_SECRET: 'test_secret_for_unit_tests_12345678',
    };

    const validated = validate(config);
    expect(validated.NODE_ENV).toBe(Environment.Development);
    expect(validated.PORT).toBe(3001);
    expect(validated.MQTT_PORT).toBe(1883);
    expect(validated.DATABASE_URL).toBe('postgresql://user:pass@localhost:5432/aeroponics');
    expect(validated.MQTT_CLIENT_ID).toBe('aeroponics_backend_service');
  });

  it('should throw error in production mode if DATABASE_URL and DB_HOST are missing', () => {
    const config = {
      NODE_ENV: 'production',
      PORT: '3001',
    };

    expect(() => validate(config)).toThrow(
      'Configurable environment error: Missing DATABASE_URL (or DB_HOST) in production mode.',
    );
  });

  it('should throw validation error when an invalid NODE_ENV is provided', () => {
    const config = {
      NODE_ENV: 'invalid_env_name',
    };
    expect(() => validate(config)).toThrow('Environment validation error');
  });

  it('should validate Tuya configuration variables with default and custom values', () => {
    const config = {
      NODE_ENV: 'development',
      TUYA_DEVICE_IP: '192.168.1.150',
      TUYA_DEVICE_ID: 'bf9102847291048201',
      TUYA_LOCAL_KEY: 'abc123def4567890',
      TUYA_SENSOR_ID: 'custom-w218-test',
      TUYA_ON_DEMAND_TIMEOUT_MS: '8000',
      TUYA_COOLDOWN_WINDOW_MS: '45000',
    };

    const validated = validate(config);
    expect(validated.TUYA_DEVICE_IP).toBe('192.168.1.150');
    expect(validated.TUYA_DEVICE_ID).toBe('bf9102847291048201');
    expect(validated.TUYA_LOCAL_KEY).toBe('abc123def4567890');
    expect(validated.TUYA_SENSOR_ID).toBe('custom-w218-test');
    expect(validated.TUYA_ON_DEMAND_TIMEOUT_MS).toBe(8000);
    expect(validated.TUYA_COOLDOWN_WINDOW_MS).toBe(45000);
    expect(validated.TUYA_BRIDGE_ENABLED).toBe(false);

    const enabledConfig = {
      NODE_ENV: 'development',
      TUYA_BRIDGE_ENABLED: 'true',
    };
    expect(validate(enabledConfig).TUYA_BRIDGE_ENABLED).toBe(true);

    const defaultConfig = {
      NODE_ENV: 'development',
    };
    const defaultValidated = validate(defaultConfig);
    expect(defaultValidated.TUYA_SENSOR_ID).toBe('ph-w218-01');
    expect(defaultValidated.TUYA_ON_DEMAND_TIMEOUT_MS).toBe(5000);
    expect(defaultValidated.TUYA_COOLDOWN_WINDOW_MS).toBe(60000);
    expect(defaultValidated.TUYA_BRIDGE_ENABLED).toBe(false);
  });
});

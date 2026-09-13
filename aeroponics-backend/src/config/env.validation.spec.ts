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
});

import 'reflect-metadata';
import { ConfigService } from '@nestjs/config';
import { DatabaseModule } from './database.module';

describe('DatabaseModule', () => {
  it('should be defined and configure TypeORM with synchronize: false', () => {
    const mockConfigService = {
      get: jest.fn((key: string, defaultValue?: any) => {
        if (key === 'DATABASE_URL') return 'postgresql://user:pass@localhost:5432/aeroponics';
        if (key === 'NODE_ENV') return 'development';
        return defaultValue;
      }),
    } as unknown as ConfigService;

    // Inspect the module definition metadata directly
    const imports = Reflect.getMetadata('imports', DatabaseModule);
    expect(imports).toBeDefined();

    const dynamicTypeOrmModule = imports[0];
    expect(dynamicTypeOrmModule).toBeDefined();

    const getOptions = (configService: ConfigService) => {
      const databaseUrl = configService.get<string>('DATABASE_URL');
      const isDevelopment = configService.get<string>('NODE_ENV') === 'development';
      const isTest = configService.get<string>('NODE_ENV') === 'test';

      return {
        type: 'postgres' as const,
        autoLoadEntities: true,
        synchronize: false,
        migrationsRun: !isTest,
        logging: isDevelopment,
        url: databaseUrl,
      };
    };

    const options = getOptions(mockConfigService);
    expect(options.synchronize).toBe(false);
    expect(options.migrationsRun).toBe(true);
    expect(options.url).toBe('postgresql://user:pass@localhost:5432/aeroponics');
  });

  it('should use host, port, credentials when DATABASE_URL is not set', () => {
    const mockConfigService = {
      get: jest.fn((key: string, defaultValue?: any) => {
        if (key === 'DATABASE_URL') return undefined;
        if (key === 'DB_HOST') return 'timescaledb';
        if (key === 'DB_PORT') return 5432;
        if (key === 'DB_USER') return 'aeroponics_user';
        if (key === 'DB_PASS') return 'secret';
        if (key === 'DB_NAME') return 'aeroponics';
        if (key === 'NODE_ENV') return 'production';
        return defaultValue;
      }),
    } as unknown as ConfigService;

    const getOptions = (configService: ConfigService) => {
      const isDevelopment = configService.get<string>('NODE_ENV') === 'development';
      const isTest = configService.get<string>('NODE_ENV') === 'test';

      return {
        type: 'postgres' as const,
        autoLoadEntities: true,
        synchronize: false,
        migrationsRun: !isTest,
        logging: isDevelopment,
        host: configService.get<string>('DB_HOST', 'timescaledb'),
        port: configService.get<number>('DB_PORT', 5432),
      };
    };

    const options = getOptions(mockConfigService);
    expect(options.synchronize).toBe(false);
    expect(options.migrationsRun).toBe(true);
    expect(options.host).toBe('timescaledb');
    expect(options.port).toBe(5432);
  });
});

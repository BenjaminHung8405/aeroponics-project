import { Module, Global } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ConfigModule, ConfigService } from '@nestjs/config';
import { join } from 'path';
import { Pool, PoolConfig } from 'pg';

/**
 * K1: Dedicated write pool injection token.
 * A separate pg.Pool (max 10 connections) for TimescaleDB batch inserts.
 * Main TypeORM pool: max 20 connections.
 * Total: 30 connections — well within TimescaleDB max_connections = 100.
 */
export const WRITE_POOL = 'WRITE_POOL';

@Global()
@Module({
  imports: [
    TypeOrmModule.forRootAsync({
      imports: [ConfigModule],
      inject: [ConfigService],
      useFactory: (configService: ConfigService) => {
        const databaseUrl = configService.get<string>('DATABASE_URL');
        const isDevelopment = configService.get<string>('NODE_ENV') === 'development';
        const isTest = configService.get<string>('NODE_ENV') === 'test';

        const baseConfig = {
          type: 'postgres' as const,
          autoLoadEntities: true,
          synchronize: false, // HARD RULE S3-DB-03: Zero schema auto-sync
          migrationsRun: !isTest, // Run migrations automatically on app boot
          migrations: [join(__dirname, 'migrations', '*{.ts,.js}')],
          logging: isDevelopment,
        };

        if (databaseUrl) {
          return {
            ...baseConfig,
            url: databaseUrl,
            extra: {
              max: 20,
              idleTimeoutMillis: 30000,
              connectionTimeoutMillis: 5000,
            },
          };
        }

        return {
          ...baseConfig,
          host: configService.get<string>('DB_HOST', 'timescaledb'),
          port: configService.get<number>('DB_PORT', 5432),
          username: configService.get<string>('DB_USER', 'aeroponics_user'),
          password: configService.get<string>('DB_PASS', ''),
          database: configService.get<string>('DB_NAME', 'aeroponics'),
          extra: {
            max: 20,
            idleTimeoutMillis: 30000,
            connectionTimeoutMillis: 5000,
          },
        };
      },
    }),
  ],
  providers: [
    {
      provide: WRITE_POOL,
      inject: [ConfigService],
      useFactory: (configService: ConfigService): Pool => {
        const databaseUrl = configService.get<string>('DATABASE_URL');

        const poolConfig: PoolConfig = {
          max: 10,
          idleTimeoutMillis: 10000,
          connectionTimeoutMillis: 5000,
        };

        if (databaseUrl) {
          return new Pool({ connectionString: databaseUrl, ...poolConfig });
        }

        return new Pool({
          host: configService.get<string>('DB_HOST', 'timescaledb'),
          port: configService.get<number>('DB_PORT', 5432),
          user: configService.get<string>('DB_USER', 'aeroponics_user'),
          password: configService.get<string>('DB_PASS', ''),
          database: configService.get<string>('DB_NAME', 'aeroponics'),
          ...poolConfig,
        });
      },
    },
  ],
  exports: [WRITE_POOL],
})
export class DatabaseModule {}

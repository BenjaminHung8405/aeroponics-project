import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ConfigModule, ConfigService } from '@nestjs/config';
import { join } from 'path';

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
          extra: {
            max: 20,
            idleTimeoutMillis: 30000,
            connectionTimeoutMillis: 5000,
          },
        };

        if (databaseUrl) {
          return {
            ...baseConfig,
            url: databaseUrl,
          };
        }

        return {
          ...baseConfig,
          host: configService.get<string>('DB_HOST', 'timescaledb'),
          port: configService.get<number>('DB_PORT', 5432),
          username: configService.get<string>('DB_USER', 'aeroponics_user'),
          password: configService.get<string>('DB_PASS', ''),
          database: configService.get<string>('DB_NAME', 'aeroponics'),
        };
      },
    }),
  ],
})
export class DatabaseModule {}

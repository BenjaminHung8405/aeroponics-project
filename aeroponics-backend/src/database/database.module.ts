import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ConfigService } from '@nestjs/config';

@Module({
  imports: [
    TypeOrmModule.forRootAsync({
      inject: [ConfigService],
      useFactory: (configService: ConfigService) => {
        const databaseUrl = configService.get<string>('DATABASE_URL');
        if (databaseUrl) {
          return {
            type: 'postgres',
            url: databaseUrl,
            autoLoadEntities: true,
            synchronize: false,
            logging: configService.get<string>('NODE_ENV') === 'development',
          };
        }
        return {
          type: 'postgres',
          host: configService.get<string>('DB_HOST', 'timescaledb'),
          port: configService.get<number>('DB_PORT', 5432),
          username: configService.get<string>('DB_USER', 'aeroponics_user'),
          password: configService.get<string>('DB_PASS', ''),
          database: configService.get<string>('DB_NAME', 'aeroponics'),
          autoLoadEntities: true,
          synchronize: false,
          logging: configService.get<string>('NODE_ENV') === 'development',
        };
      },
    }),
  ],
})
export class DatabaseModule {}

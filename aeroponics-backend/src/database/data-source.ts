import { DataSource } from 'typeorm';
import * as dotenv from 'dotenv';
import { join } from 'path';

dotenv.config();

const databaseUrl = process.env.DATABASE_URL;

export default new DataSource(
  databaseUrl
    ? {
        type: 'postgres',
        url: databaseUrl,
        synchronize: false,
        migrationsRun: false,
        entities: [join(__dirname, '..', '**', '*.entity{.ts,.js}')],
        migrations: [join(__dirname, 'migrations', '*{.ts,.js}')],
      }
    : {
        type: 'postgres',
        host: process.env.DB_HOST || 'timescaledb',
        port: parseInt(process.env.DB_PORT || '5432', 10),
        username: process.env.DB_USER || 'aeroponics_user',
        password: process.env.DB_PASS || '',
        database: process.env.DB_NAME || 'aeroponics',
        synchronize: false,
        migrationsRun: false,
        entities: [join(__dirname, '..', '**', '*.entity{.ts,.js}')],
        migrations: [join(__dirname, 'migrations', '*{.ts,.js}')],
      },
);

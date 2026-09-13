import { Controller, Get, Header, ServiceUnavailableException } from '@nestjs/common';
import { DataSource } from 'typeorm';
import * as fs from 'fs';
import * as path from 'path';
import { Public } from './auth/public.decorator';

@Controller()
export class AppController {
  constructor(private readonly dataSource: DataSource) {}

  @Public()
  @Get()
  @Header('Content-Type', 'text/html; charset=utf-8')
  getIndex(): string {
    const candidates = [
      path.join(__dirname, '..', '..', 'aeroponics-ui', 'index.html'),
      path.join(process.cwd(), 'aeroponics-ui', 'index.html'),
      path.join(process.cwd(), '..', 'aeroponics-ui', 'index.html'),
      path.join(__dirname, '..', 'public', 'index.html'),
      path.join(__dirname, 'public', 'index.html'),
      path.join(process.cwd(), 'public', 'index.html'),
      path.join(process.cwd(), 'aeroponics-backend', 'public', 'index.html'),
    ];
    for (const p of candidates) {
      if (fs.existsSync(p)) {
        return fs.readFileSync(p, 'utf8');
      }
    }
    return '<!DOCTYPE html><html><body><h1>Aeroponics Smart Farm</h1></body></html>';
  }

  @Public()
  @Get('health')
  async getHealth(): Promise<{ status: string; db: string }> {
    try {
      if (!this.dataSource.isInitialized) {
        throw new Error('Database connection is not initialized');
      }
      await this.dataSource.query('SELECT 1');
      return { status: 'ok', db: 'connected' };
    } catch (error) {
      throw new ServiceUnavailableException({
        status: 'error',
        db: 'disconnected',
        message: error instanceof Error ? error.message : 'Database ping failed',
      });
    }
  }
}


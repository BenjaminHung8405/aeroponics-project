import { Controller, Get, ServiceUnavailableException } from '@nestjs/common';
import { DataSource } from 'typeorm';
import { Public } from './auth/public.decorator';

@Controller()
export class AppController {
  constructor(private readonly dataSource: DataSource) {}

  @Public()
  @Get(['health', 'api/health'])
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


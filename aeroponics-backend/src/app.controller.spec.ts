import { Test, TestingModule } from '@nestjs/testing';
import { ServiceUnavailableException } from '@nestjs/common';
import { DataSource } from 'typeorm';
import { AppController } from './app.controller';

describe('AppController', () => {
  let appController: AppController;
  let mockDataSource: {
    isInitialized: boolean;
    query: jest.Mock;
  };

  beforeEach(async () => {
    mockDataSource = {
      isInitialized: true,
      query: jest.fn().mockResolvedValue([{ '?column?': 1 }]),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [AppController],
      providers: [
        {
          provide: DataSource,
          useValue: mockDataSource,
        },
      ],
    }).compile();

    appController = module.get<AppController>(AppController);
  });

  describe('getHealth', () => {
    it('should return status ok and db connected when database ping succeeds', async () => {
      const result = await appController.getHealth();
      expect(result).toEqual({ status: 'ok', db: 'connected' });
      expect(mockDataSource.query).toHaveBeenCalledWith('SELECT 1');
    });

    it('should throw ServiceUnavailableException (503) when database is not initialized', async () => {
      mockDataSource.isInitialized = false;

      await expect(appController.getHealth()).rejects.toThrow(
        ServiceUnavailableException,
      );
    });

    it('should throw ServiceUnavailableException (503) when database query fails', async () => {
      mockDataSource.query.mockRejectedValue(new Error('Connection terminated'));

      await expect(appController.getHealth()).rejects.toThrow(
        ServiceUnavailableException,
      );

      try {
        await appController.getHealth();
      } catch (err: any) {
        expect(err.getStatus()).toBe(503);
        expect(err.getResponse()).toEqual({
          status: 'error',
          db: 'disconnected',
          message: 'Connection terminated',
        });
      }
    });
  });
});

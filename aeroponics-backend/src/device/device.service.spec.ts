import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { NotFoundException } from '@nestjs/common';
import { DeviceService } from './device.service';
import { DeviceStatus } from './entities/device_status.entity';

describe('DeviceService', () => {
  let service: DeviceService;
  let mockDeviceStatusRepo: {
    findOne: jest.Mock;
  };

  beforeEach(async () => {
    mockDeviceStatusRepo = {
      findOne: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        DeviceService,
        {
          provide: getRepositoryToken(DeviceStatus),
          useValue: mockDeviceStatusRepo,
        },
      ],
    }).compile();

    service = module.get<DeviceService>(DeviceService);
  });

  describe('getDeviceStatus', () => {
    it('should return device status if device exists', async () => {
      const mockStatus: Partial<DeviceStatus> = {
        device_id: 'esp32_gw_01',
        status: 'online',
        uptime_s: '3600',
        rssi_dbm: -65,
        free_heap_b: 184000,
        ntp_synced: true,
        rtc_valid: true,
        last_seen_at: new Date('2026-09-13T10:00:00Z'),
      };
      mockDeviceStatusRepo.findOne.mockResolvedValue(mockStatus);

      const result = await service.getDeviceStatus('esp32_gw_01');

      expect(mockDeviceStatusRepo.findOne).toHaveBeenCalledWith({
        where: { device_id: 'esp32_gw_01' },
      });
      expect(result).toEqual({
        device_id: 'esp32_gw_01',
        status: 'online',
        uptime_s: 3600,
        rssi_dbm: -65,
        free_heap_b: 184000,
        ntp_synced: true,
        rtc_valid: true,
        last_seen_at: new Date('2026-09-13T10:00:00Z'),
      });
    });

    it('should throw NotFoundException if device is not found', async () => {
      mockDeviceStatusRepo.findOne.mockResolvedValue(null);

      await expect(service.getDeviceStatus('unknown_device')).rejects.toThrow(
        NotFoundException,
      );
    });
  });
});

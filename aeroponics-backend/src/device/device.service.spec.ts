import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { NotFoundException } from '@nestjs/common';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { DeviceService } from './device.service';
import { DeviceStatus } from './entities/device_status.entity';

describe('DeviceService', () => {
  let service: DeviceService;
  let mockDeviceStatusRepo: {
    findOne: jest.Mock;
    find: jest.Mock;
    save: jest.Mock;
  };
  let mockEventEmitter: {
    emit: jest.Mock;
  };

  beforeEach(async () => {
    mockDeviceStatusRepo = {
      findOne: jest.fn(),
      find: jest.fn(),
      save: jest.fn(),
    };
    mockEventEmitter = {
      emit: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        DeviceService,
        {
          provide: getRepositoryToken(DeviceStatus),
          useValue: mockDeviceStatusRepo,
        },
        {
          provide: EventEmitter2,
          useValue: mockEventEmitter,
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

  describe('getAllDevicesStatus', () => {
    it('should return all device statuses if records exist', async () => {
      const mockList: Partial<DeviceStatus>[] = [
        {
          device_id: 'esp32_device',
          status: 'online',
          uptime_s: '120',
          rssi_dbm: -55,
          free_heap_b: 190000,
          ntp_synced: true,
          rtc_valid: true,
          last_seen_at: new Date('2026-09-18T10:00:00Z'),
        },
      ];
      mockDeviceStatusRepo.find.mockResolvedValue(mockList);

      const result = await service.getAllDevicesStatus();
      expect(result).toHaveLength(1);
      expect(result[0].device_id).toBe('esp32_device');
      expect(result[0].status).toBe('online');
    });

    it('should return fallback offline default if no records exist', async () => {
      mockDeviceStatusRepo.find.mockResolvedValue([]);

      const result = await service.getAllDevicesStatus();
      expect(result).toHaveLength(1);
      expect(result[0].device_id).toBe('esp32_device');
      expect(result[0].status).toBe('offline');
    });
  });

  describe('checkDeviceStaleness', () => {
    it('should mark online device as offline if heartbeat timed out', async () => {
      const staleDate = new Date(Date.now() - 40000); // 40s ago > 30s
      const mockDevice: Partial<DeviceStatus> = {
        device_id: 'esp32_device',
        status: 'online',
        uptime_s: '100',
        last_seen_at: staleDate,
      };
      mockDeviceStatusRepo.find.mockResolvedValue([mockDevice]);

      await service.checkDeviceStaleness(30000);

      expect(mockDevice.status).toBe('offline');
      expect(mockDeviceStatusRepo.save).toHaveBeenCalledWith(mockDevice);
      expect(mockEventEmitter.emit).toHaveBeenCalledWith(
        'device.status_changed',
        expect.objectContaining({
          deviceId: 'esp32_device',
          status: 'offline',
          reason: 'HEARTBEAT_TIMEOUT',
        }),
      );
    });
  });
});

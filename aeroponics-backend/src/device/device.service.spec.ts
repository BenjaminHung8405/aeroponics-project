import { Test, TestingModule } from '@nestjs/testing';
import { getRepositoryToken } from '@nestjs/typeorm';
import { NotFoundException } from '@nestjs/common';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { DeviceService } from './device.service';
import { Device } from './entities/device.entity';
import { DeviceStatus } from './entities/device_status.entity';

import { NodeService } from '../node/node.service';

describe('DeviceService', () => {
  let service: DeviceService;
  let mockDeviceStatusRepo: {
    findOne: jest.Mock;
    find: jest.Mock;
    save: jest.Mock;
  };
  let mockDeviceRepo: {
    findOne: jest.Mock;
    find: jest.Mock;
    create: jest.Mock;
    save: jest.Mock;
  };
  let mockEventEmitter: {
    emit: jest.Mock;
  };
  let mockNodeService: {
    checkStaleness: jest.Mock;
  };

  beforeEach(async () => {
    mockDeviceStatusRepo = {
      findOne: jest.fn(),
      find: jest.fn(),
      save: jest.fn(),
    };
    mockDeviceRepo = {
      findOne: jest.fn(),
      find: jest.fn(),
      create: jest.fn((dto) => ({ ...dto })),
      save: jest.fn((entity) => Promise.resolve({ ...entity })),
    };
    mockEventEmitter = {
      emit: jest.fn(),
    };
    mockNodeService = {
      checkStaleness: jest.fn().mockResolvedValue(undefined),
    };

    const module: TestingModule = await Test.createTestingModule({
      providers: [
        DeviceService,
        {
          provide: getRepositoryToken(DeviceStatus),
          useValue: mockDeviceStatusRepo,
        },
        {
          provide: getRepositoryToken(Device),
          useValue: mockDeviceRepo,
        },
        {
          provide: EventEmitter2,
          useValue: mockEventEmitter,
        },
        {
          provide: NodeService,
          useValue: mockNodeService,
        },
      ],
    }).compile();

    service = module.get<DeviceService>(DeviceService);

  });

  afterEach(() => {
    service.onModuleDestroy();
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
        time_source: 'DS1307_RTC',
        last_sync_unix_time_utc: '1757752800',
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
        time_source: 'DS1307_RTC',
        last_sync_unix_time_utc: 1757752800,
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
          device_id: 'aero_s3_b81f3fbbcf3c',
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
      expect(result[0].device_id).toBe('aero_s3_b81f3fbbcf3c');
      expect(result[0].status).toBe('online');
    });

    it('should return empty list if no records exist', async () => {
      mockDeviceStatusRepo.find.mockResolvedValue([]);

      const result = await service.getAllDevicesStatus();
      expect(result).toEqual([]);
    });
  });

  describe('checkDeviceStaleness', () => {
    it('should mark online device as offline if heartbeat timed out', async () => {
      const staleDate = new Date(Date.now() - 40000); // 40s ago > 30s
      const mockDevice: Partial<DeviceStatus> = {
        device_id: 'aero_s3_b81f3fbbcf3c',
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
          deviceId: 'aero_s3_b81f3fbbcf3c',
          status: 'offline',
          reason: 'HEARTBEAT_TIMEOUT',
        }),
      );
    });
  });

  describe('syncDeviceClock', () => {
    it('should push time to device and return success', async () => {
      const mockDevice: Partial<DeviceStatus> = {
        device_id: 'aero_s3_b81f3fbbcf3c',
        status: 'online',
      };
      mockDeviceStatusRepo.findOne.mockResolvedValue(mockDevice);

      const result = await service.syncDeviceClock('aero_s3_b81f3fbbcf3c');

      expect(result.success).toBe(true);
      expect(result.device_id).toBe('aero_s3_b81f3fbbcf3c');
      expect(typeof result.timestamp).toBe('number');
    });

    it('should throw NotFoundException if device does not exist', async () => {
      mockDeviceStatusRepo.findOne.mockResolvedValue(null);

      await expect(service.syncDeviceClock('non_existent')).rejects.toThrow(
        NotFoundException,
      );
    });
  });

  describe('ensureDeviceRegistered & handleDeviceStatusChanged', () => {
    it('should auto-provision new MAC-derived device and emit device.registered', async () => {
      mockDeviceRepo.findOne.mockResolvedValue(null);

      const dev = await service.ensureDeviceRegistered('aero_s3_3485188f12a0');

      expect(dev).not.toBeNull();
      expect(dev?.device_id).toBe('aero_s3_3485188f12a0');
      expect(dev?.display_name).toBe('ESP32 (8f12a0)');
      expect(dev?.enabled).toBe(true);
      expect(mockDeviceRepo.create).toHaveBeenCalledWith(
        expect.objectContaining({
          device_id: 'aero_s3_3485188f12a0',
          display_name: 'ESP32 (8f12a0)',
          enabled: true,
        }),
      );
      expect(mockDeviceRepo.save).toHaveBeenCalled();
      expect(mockEventEmitter.emit).toHaveBeenCalledWith('device.registered', {
        deviceId: 'aero_s3_3485188f12a0',
      });
    });

    it('should return existing device without re-inserting or re-emitting', async () => {
      const existing = {
        device_id: 'esp32_field',
        display_name: 'ESP32 Thực địa',
        enabled: true,
      };
      mockDeviceRepo.findOne.mockResolvedValue(existing);

      const dev = await service.ensureDeviceRegistered('esp32_field');

      expect(dev).toBe(existing);
      expect(mockDeviceRepo.create).not.toHaveBeenCalled();
      expect(mockEventEmitter.emit).not.toHaveBeenCalledWith(
        'device.registered',
        expect.anything(),
      );
    });

    it('should trigger ensureDeviceRegistered on device.status_changed event', async () => {
      const spy = jest.spyOn(service, 'ensureDeviceRegistered').mockResolvedValue({} as any);

      await service.handleDeviceStatusChanged({ deviceId: 'aero_s3_112233' });

      expect(spy).toHaveBeenCalledWith('aero_s3_112233');
    });
  });

  describe('updateDevice', () => {
    it('should update display_name and enabled', async () => {
      const existing = {
        device_id: 'aero_s3_3485188f12a0',
        display_name: 'ESP32 (8f12a0)',
        enabled: true,
      };
      mockDeviceRepo.findOne.mockResolvedValue(existing);

      const updated = await service.updateDevice('aero_s3_3485188f12a0', {
        display_name: 'ESP32 Trạm Khí Canh Vườn 1',
        enabled: false,
      });

      expect(updated.display_name).toBe('ESP32 Trạm Khí Canh Vườn 1');
      expect(updated.enabled).toBe(false);
      expect(mockDeviceRepo.save).toHaveBeenCalledWith(
        expect.objectContaining({
          display_name: 'ESP32 Trạm Khí Canh Vườn 1',
          enabled: false,
        }),
      );
    });

    it('should throw NotFoundException if device cannot be resolved', async () => {
      mockDeviceRepo.findOne.mockResolvedValue(null);
      // Simulate failure in ensureDeviceRegistered by having repository absent
      const serviceWithoutRepo = new DeviceService(
        mockDeviceStatusRepo as any,
        mockEventEmitter as any,
        mockNodeService as any,
      );

      await expect(
        serviceWithoutRepo.updateDevice('unknown', { display_name: 'Test' }),
      ).rejects.toThrow(NotFoundException);
    });
  });
});

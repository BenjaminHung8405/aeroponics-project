import { Test, TestingModule } from '@nestjs/testing';
import { NotFoundException } from '@nestjs/common';
import { DeviceController } from './device.controller';
import { DeviceService } from './device.service';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { ScheduleStateSyncService } from '../mqtt/schedule-state-sync.service';

describe('DeviceController', () => {
  let controller: DeviceController;
  let mockDeviceService: {
    getDeviceStatus: jest.Mock;
    getAllDevicesStatus: jest.Mock;
    syncDeviceClock: jest.Mock;
    updateDevice: jest.Mock;
  };
  let mockScheduleStateSyncService: { retryManually: jest.Mock };


  beforeEach(async () => {
    mockDeviceService = {
      getDeviceStatus: jest.fn(),
      getAllDevicesStatus: jest.fn(),
      syncDeviceClock: jest.fn(),
      updateDevice: jest.fn(),
    };
    mockScheduleStateSyncService = {
      retryManually: jest.fn(),
    };


    const module: TestingModule = await Test.createTestingModule({
      controllers: [DeviceController],
      providers: [
        {
          provide: DeviceService,
          useValue: mockDeviceService,
        },
        {
          provide: ScheduleStateSyncService,
          useValue: mockScheduleStateSyncService,
        },
      ],
    })
      .overrideGuard(JwtAuthGuard)
      .useValue({ canActivate: () => true })
      .compile();

    controller = module.get<DeviceController>(DeviceController);
  });

  describe('getDeviceStatus', () => {
    it('should return device status response dto', async () => {
      const mockResult = {
        device_id: 'esp32_gw_01',
        status: 'online',
        uptime_s: 120,
        rssi_dbm: -50,
        free_heap_b: 200000,
        ntp_synced: true,
        rtc_valid: true,
        last_seen_at: new Date(),
      };
      mockDeviceService.getDeviceStatus.mockResolvedValue(mockResult);

      const result = await controller.getDeviceStatus('esp32_gw_01');
      expect(result).toBe(mockResult);
      expect(mockDeviceService.getDeviceStatus).toHaveBeenCalledWith(
        'esp32_gw_01',
      );
    });

    it('should propagate NotFoundException if device not found', async () => {
      mockDeviceService.getDeviceStatus.mockRejectedValue(
        new NotFoundException('Device status not found for device: unknown'),
      );

      await expect(controller.getDeviceStatus('unknown')).rejects.toThrow(
        NotFoundException,
      );
    });

    it('should have JwtAuthGuard applied at controller level', () => {
      const guards = Reflect.getMetadata('__guards__', DeviceController);
      expect(guards).toBeDefined();
      expect(guards.length).toBeGreaterThan(0);
      expect(guards[0]).toBe(JwtAuthGuard);
    });
  });

  describe('getAllDevicesStatus', () => {
    it('should return array of device status response dtos', async () => {
      const mockList = [
        {
          device_id: 'esp32_device',
          status: 'online',
          uptime_s: 120,
          rssi_dbm: -50,
          free_heap_b: 200000,
          ntp_synced: true,
          rtc_valid: true,
          last_seen_at: new Date(),
        },
      ];
      mockDeviceService.getAllDevicesStatus = jest.fn().mockResolvedValue(mockList);

      const result = await controller.getAllDevicesStatus();
      expect(result).toBe(mockList);
      expect(mockDeviceService.getAllDevicesStatus).toHaveBeenCalled();
    });
  });

  describe('syncDeviceClock', () => {
    it('should call deviceService.syncDeviceClock and return result', async () => {
      const mockResult = {
        success: true,
        device_id: 'esp32_gw_01',
        timestamp: 1727500000,
      };
      mockDeviceService.syncDeviceClock = jest.fn().mockResolvedValue(mockResult);

      const result = await controller.syncDeviceClock('esp32_gw_01');
      expect(result).toBe(mockResult);
      expect(mockDeviceService.syncDeviceClock).toHaveBeenCalledWith(
        'esp32_gw_01',
      );
    });
  });

  describe('updateDevice', () => {
    it('should call deviceService.updateDevice and return updated entity', async () => {
      const mockUpdated = {
        device_id: 'aero_s3_3485188f12a0',
        display_name: 'ESP32 Trạm Khí Canh Vườn 1',
        enabled: true,
      };
      mockDeviceService.updateDevice.mockResolvedValue(mockUpdated);

      const result = await controller.updateDevice('aero_s3_3485188f12a0', {
        display_name: 'ESP32 Trạm Khí Canh Vườn 1',
      });

      expect(result).toBe(mockUpdated);
      expect(mockDeviceService.updateDevice).toHaveBeenCalledWith(
        'aero_s3_3485188f12a0',
        { display_name: 'ESP32 Trạm Khí Canh Vườn 1' },
      );
    });
  });
});

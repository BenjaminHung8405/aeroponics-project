import { Test, TestingModule } from '@nestjs/testing';
import { NotFoundException } from '@nestjs/common';
import { DeviceController } from './device.controller';
import { DeviceService } from './device.service';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';

describe('DeviceController', () => {
  let controller: DeviceController;
  let mockDeviceService: {
    getDeviceStatus: jest.Mock;
  };

  beforeEach(async () => {
    mockDeviceService = {
      getDeviceStatus: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [DeviceController],
      providers: [
        {
          provide: DeviceService,
          useValue: mockDeviceService,
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
});

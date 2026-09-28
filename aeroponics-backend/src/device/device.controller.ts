import { Controller, Get, Param, Post, UseGuards } from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { DeviceService } from './device.service';
import { DeviceStatusResponseDto } from './dto/device-status-response.dto';

@Controller('api/device')
@UseGuards(JwtAuthGuard)
export class DeviceController {
  constructor(private readonly deviceService: DeviceService) {}

  @Get('status')
  async getAllDevicesStatus(): Promise<DeviceStatusResponseDto[]> {
    return this.deviceService.getAllDevicesStatus();
  }

  @Get(':id/status')
  async getDeviceStatus(
    @Param('id') id: string,
  ): Promise<DeviceStatusResponseDto> {
    return this.deviceService.getDeviceStatus(id);
  }

  @Post(':id/sync-clock')
  async syncDeviceClock(
    @Param('id') id: string,
  ): Promise<{ success: boolean; device_id: string; timestamp: number }> {
    return this.deviceService.syncDeviceClock(id);
  }
}

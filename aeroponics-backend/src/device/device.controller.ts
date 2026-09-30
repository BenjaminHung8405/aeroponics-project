import { Body, Controller, Get, Param, Patch, Post, UseGuards } from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { DeviceService } from './device.service';
import { DeviceStatusResponseDto } from './dto/device-status-response.dto';
import { UpdateDeviceDto } from './dto/update-device.dto';
import { Device } from './entities/device.entity';

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

  @Patch(':id')
  async updateDevice(
    @Param('id') id: string,
    @Body() dto: UpdateDeviceDto,
  ): Promise<Device> {
    return this.deviceService.updateDevice(id, dto);
  }

  @Post(':id/sync-clock')
  async syncDeviceClock(
    @Param('id') id: string,
  ): Promise<{ success: boolean; device_id: string; timestamp: number }> {
    return this.deviceService.syncDeviceClock(id);
  }
}


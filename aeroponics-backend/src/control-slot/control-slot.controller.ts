import {
  Body,
  Controller,
  Get,
  HttpCode,
  HttpStatus,
  Param,
  ParseIntPipe,
  Put,
  Req,
  UseGuards,
} from '@nestjs/common';
import type { Request } from 'express';
import { ConfigService } from '@nestjs/config';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { ControlSlotService } from './control-slot.service';
import { UpdateControlSlotDto } from './dto/update-control-slot.dto';

interface AuthenticatedRequest extends Request {
  user?: { userId?: string; username?: string; device_id?: string };
}

@Controller('api/control-slot')
@UseGuards(JwtAuthGuard)
export class ControlSlotController {
  constructor(
    private readonly service: ControlSlotService,
    private readonly configService: ConfigService,
  ) {}

  @Get()
  @HttpCode(HttpStatus.OK)
  getSlots(@Req() request: AuthenticatedRequest) {
    return this.service.getSlots(this.deviceId(request));
  }

  @Put(':slotIndex')
  @HttpCode(HttpStatus.OK)
  updateSlot(
    @Param('slotIndex', ParseIntPipe) slotIndex: number,
    @Body() dto: UpdateControlSlotDto,
    @Req() request: AuthenticatedRequest,
  ) {
    const deviceId = this.deviceId(request);
    return this.service.updateSlot(deviceId, slotIndex, dto, request.user?.username ?? deviceId);
  }

  private deviceId(request: AuthenticatedRequest): string {
    return request.user?.device_id ?? this.configService.get<string>('MQTT_DEVICE_ID', 'esp32_device');
  }
}

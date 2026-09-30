import {
  Body,
  BadRequestException,
  Controller,
  Get,
  HttpCode,
  HttpStatus,
  Param,
  ParseIntPipe,
  Put,
  Query,
  Req,
  UseGuards,
} from '@nestjs/common';
import type { Request } from 'express';
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
  ) {}

  @Get()
  @HttpCode(HttpStatus.OK)
  getSlots(
    @Req() request: AuthenticatedRequest,
    @Query('deviceId') deviceIdQuery?: string,
  ) {
    return this.service.getSlots(this.deviceId(request, deviceIdQuery));
  }

  @Put(':slotIndex')
  @HttpCode(HttpStatus.OK)
  updateSlot(
    @Param('slotIndex', ParseIntPipe) slotIndex: number,
    @Body() dto: UpdateControlSlotDto,
    @Req() request: AuthenticatedRequest,
    @Query('deviceId') deviceIdQuery?: string,
  ) {
    const deviceId = this.deviceId(request, deviceIdQuery);
    return this.service.updateSlot(deviceId, slotIndex, dto, request.user?.username ?? deviceId);
  }

  private deviceId(request: AuthenticatedRequest, deviceIdQuery?: string): string {
    const deviceId = deviceIdQuery || request.user?.device_id;
    if (!deviceId) {
      throw new BadRequestException('deviceId is required for device-scoped control-slot operations');
    }
    return deviceId;
  }
}

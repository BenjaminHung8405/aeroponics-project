import {
  Controller,
  Get,
  Put,
  Param,
  Query,
  Body,
  Req,
  ParseIntPipe,
  UseGuards,
  HttpCode,
  HttpStatus,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import {
  FlowService,
  FlowHistoryResponse,
  NodeCalibrationResponse,
} from './flow.service';
import { FlowHistoryQueryDto } from './dto/flow-history-query.dto';
import { UpdateCalibrationDto } from './dto/update-calibration.dto';
import { SensorCalibration } from '../node/entities/sensor_calibration.entity';

@Controller('api/node')
@UseGuards(JwtAuthGuard)
export class FlowController {
  constructor(private readonly flowService: FlowService) {}

  /**
   * S3-G2: Get historical flow readings and summary metrics for a node.
   * Efficient range query with time pruning. Default hours = 24, maximum hours = 720.
   */
  @Get(':id/flow')
  @HttpCode(HttpStatus.OK)
  async getFlowHistory(
    @Param('id', ParseIntPipe) id: number,
    @Query() query: FlowHistoryQueryDto,
  ): Promise<FlowHistoryResponse> {
    return this.flowService.getHistory(id, query.hours, query.limit);
  }

  /**
   * S3-G2: Get active calibration and audit version history for a node.
   */
  @Get(':id/calibration')
  @HttpCode(HttpStatus.OK)
  async getCalibration(
    @Param('id', ParseIntPipe) id: number,
  ): Promise<NodeCalibrationResponse> {
    return this.flowService.getCalibration(id);
  }

  /**
   * S3-G2: Update flow sensor calibration with versioned audit log and user identity.
   */
  @Put(':id/calibration')
  @HttpCode(HttpStatus.OK)
  async updateCalibration(
    @Param('id', ParseIntPipe) id: number,
    @Body() dto: UpdateCalibrationDto,
    @Req() req: any,
  ): Promise<SensorCalibration> {
    const operator =
      req?.user?.username || req?.user?.sub || dto.calibrated_by || 'operator';
    return this.flowService.updateCalibration(id, dto, operator);
  }
}

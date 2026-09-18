import {
  Controller,
  Post,
  Get,
  Body,
  Query,
  Req,
  UseGuards,
  HttpCode,
  HttpStatus,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { TuyaBridgeService } from './tuya-bridge.service';
import { TriggerMeasurementDto } from './dto/trigger-measurement.dto';
import { MeasurementHistoryQueryDto } from './dto/measurement-history-query.dto';
import {
  MeasurementHistoryResponse,
  MeasurementReadingResponse,
} from './dto/measurement-response.dto';
import {
  ToggleTuyaBridgeDto,
  TuyaBridgeStatusResponse,
} from './dto/tuya-status.dto';

@Controller('api/measurement')
@UseGuards(JwtAuthGuard)
export class TuyaBridgeController {
  constructor(private readonly tuyaBridgeService: TuyaBridgeService) {}

  /**
   * S3-H3: Get Tuya PH-W218 Bridge status (Probe protection mode, static & runtime flags).
   */
  @Get('status')
  @HttpCode(HttpStatus.OK)
  async getBridgeStatus(): Promise<TuyaBridgeStatusResponse> {
    return this.tuyaBridgeService.getStatus();
  }

  /**
   * S3-H3: Toggle Tuya PH-W218 Bridge Enable/Disable state (Operator/Admin).
   */
  @Post('toggle')
  @HttpCode(HttpStatus.OK)
  async toggleBridge(
    @Body() dto: ToggleTuyaBridgeDto,
    @Req() req: any,
  ): Promise<TuyaBridgeStatusResponse> {
    const operator = req?.user?.username || req?.user?.sub || 'operator';
    return this.tuyaBridgeService.setBridgeEnabled(dto, operator);
  }

  /**
   * S3-H2: Trigger Tuya PH-W218 on-demand measurement session.
   * Rate limited with 60s cooldown window (HTTP 429 if called within cooldown).
   */
  @Post('trigger')
  @HttpCode(HttpStatus.CREATED)
  async triggerMeasurement(
    @Body() dto: TriggerMeasurementDto,
    @Req() req: any,
  ): Promise<MeasurementReadingResponse> {
    const operator = req?.user?.username || req?.user?.sub || 'operator';
    return this.tuyaBridgeService.measureOnDemand(operator, dto?.trigger_type);
  }

  /**
   * S3-H2: Get latest water quality reading from PH-W218.
   */
  @Get('latest')
  @HttpCode(HttpStatus.OK)
  async getLatestMeasurement(): Promise<MeasurementReadingResponse | null> {
    return this.tuyaBridgeService.getLatest();
  }

  /**
   * S3-H2: Get paginated historical water quality measurements.
   * Query params: limit [1..100], offset >= 0, trigger_type (ON_DEMAND | END_OF_SEASON).
   */
  @Get('history')
  @HttpCode(HttpStatus.OK)
  async getMeasurementHistory(
    @Query() query: MeasurementHistoryQueryDto,
  ): Promise<MeasurementHistoryResponse> {
    return this.tuyaBridgeService.getHistory(
      query.limit,
      query.offset,
      query.trigger_type,
    );
  }
}

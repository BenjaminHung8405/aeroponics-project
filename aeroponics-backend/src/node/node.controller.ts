import {
  Controller,
  Get,
  Put,
  Post,
  Param,
  Body,
  ParseIntPipe,
  UseGuards,
  HttpCode,
  HttpStatus,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { NodeService, NodeStatusResponse, RfScanResponse } from './node.service';
import { UpdateNodeCalibrationDto } from './dto/update-node-calibration.dto';
import { ClaimNodeDto } from './dto/claim-node.dto';

@Controller('api/node')
@UseGuards(JwtAuthGuard)
export class NodeController {
  constructor(private readonly nodeService: NodeService) {}

  @Get()
  @HttpCode(HttpStatus.OK)
  async getAllNodes(): Promise<NodeStatusResponse[]> {
    return this.nodeService.getAllNodesStatus();
  }

  @Post('scan')
  @HttpCode(HttpStatus.OK)
  async scanNodes(): Promise<RfScanResponse> {
    return this.nodeService.scanRfNodes();
  }

  @Post('claim')
  @HttpCode(HttpStatus.OK)
  async claimNode(@Body() dto: ClaimNodeDto): Promise<NodeStatusResponse> {
    return this.nodeService.claimNode(dto);
  }

  @Get(':id')
  @HttpCode(HttpStatus.OK)
  async getNodeById(
    @Param('id', ParseIntPipe) id: number,
  ): Promise<NodeStatusResponse> {
    return this.nodeService.getNodeStatus(id);
  }

  @Put(':id/calibration')
  @HttpCode(HttpStatus.OK)
  async updateCalibration(
    @Param('id', ParseIntPipe) id: number,
    @Body() dto: UpdateNodeCalibrationDto,
  ): Promise<NodeStatusResponse> {
    return this.nodeService.updateCalibration(id, dto);
  }

  @Post(':id/fault-reset')
  @HttpCode(HttpStatus.OK)
  async resetFault(
    @Param('id', ParseIntPipe) id: number,
  ): Promise<NodeStatusResponse> {
    await this.nodeService.resetFault(id);
    return this.nodeService.getNodeStatus(id);
  }
}

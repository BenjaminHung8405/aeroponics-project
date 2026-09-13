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
import { NodeService, NodeStatusResponse } from './node.service';
import { UpdateNodeCalibrationDto } from './dto/update-node-calibration.dto';

@Controller('api/node')
@UseGuards(JwtAuthGuard)
export class NodeController {
  constructor(private readonly nodeService: NodeService) {}

  @Get()
  @HttpCode(HttpStatus.OK)
  async getAllNodes(): Promise<NodeStatusResponse[]> {
    return this.nodeService.getAllNodesStatus();
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

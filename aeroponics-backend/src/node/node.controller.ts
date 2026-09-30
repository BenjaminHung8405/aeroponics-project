import {
  Controller,
  Get,
  Post,
  Param,
  Body,
  Query,
  ParseIntPipe,
  UseGuards,
  HttpCode,
  HttpStatus,
  BadRequestException,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { NodeService, NodeStatusResponse, RfScanResponse } from './node.service';
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
  async scanNodes(
    @Query('deviceId') deviceIdQuery?: string,
  ): Promise<RfScanResponse> {
    if (!deviceIdQuery) {
      throw new BadRequestException('deviceId query parameter is required for RF scan.');
    }
    return this.nodeService.scanRfNodes(deviceIdQuery);
  }

  @Post('claim')
  @HttpCode(HttpStatus.OK)
  async claimNode(
    @Body() dto: ClaimNodeDto,
    @Query('deviceId') deviceIdQuery?: string,
  ): Promise<NodeStatusResponse> {
    if (!deviceIdQuery) {
      throw new BadRequestException('deviceId query parameter is required for node claim.');
    }
    return this.nodeService.claimNode(dto, deviceIdQuery);
  }

  @Get(':id')
  @HttpCode(HttpStatus.OK)
  async getNodeById(
    @Param('id', ParseIntPipe) id: number,
  ): Promise<NodeStatusResponse> {
    return this.nodeService.getNodeStatus(id);
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

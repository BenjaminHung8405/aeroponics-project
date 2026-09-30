import {
  Controller,
  Post,
  Get,
  Param,
  Body,
  Query,
  ParseIntPipe,
  UseGuards,
  ConflictException,
  BadRequestException,
  HttpCode,
  HttpStatus,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { GroupService } from '../group/group.service';
import { PumpCommandService } from './pump-command.service';
import { PumpTargetType, SendPumpCommandDto } from './dto/send-pump-command.dto';
import { ListNodeCommandsDto } from './dto/list-node-commands.dto';
import { PumpCommand } from './entities/pump_command.entity';

@Controller('api')
@UseGuards(JwtAuthGuard)
export class PumpCommandController {
  constructor(
    private readonly pumpCommandService: PumpCommandService,
    private readonly groupService: GroupService,
  ) {}

  /**
   * S3-F4: Send pump command to a timer group.
   * Validates that the group has an ACTIVE assignment.
   * Throws 409 Conflict if the group is UNASSIGNED.
   */
  @Post('group/:groupId/command')
  @HttpCode(HttpStatus.CREATED)
  async sendGroupCommand(
    @Param('groupId', ParseIntPipe) groupId: number,
    @Body() dto: SendPumpCommandDto,
    @Query('deviceId') deviceIdQuery?: string,
  ): Promise<PumpCommand[]> {
    if (dto.target_type !== PumpTargetType.GROUP || dto.node_id !== undefined) {
      throw new BadRequestException('Group commands must target GROUP and cannot include node_id.');
    }
    if (dto.group_id !== undefined && dto.group_id !== groupId) {
      throw new BadRequestException('group_id must match the group route parameter.');
    }
    const groupStatus = await this.groupService.getGroupStatus(groupId);

    if (groupStatus.status === 'UNASSIGNED') {
      throw new ConflictException(
        `Cannot send command: Group #${groupId} is UNASSIGNED. An active treatment assignment is required.`,
      );
    }

    if (groupStatus.status !== 'ACTIVE') {
      throw new ConflictException(
        `Cannot send command: Group #${groupId} is not ACTIVE (current status: ${groupStatus.status}).`,
      );
    }

    const treatmentVersionId = groupStatus.treatment?.treatment_version_id ?? null;
    const activeNodes = groupStatus.nodes ?? [];

    if (activeNodes.length === 0) {
      throw new ConflictException(
        `Cannot send command: Group #${groupId} has no active nodes assigned.`,
      );
    }

    // Target specific node if provided, otherwise target all active nodes in the group
    if (dto.node_id) {
      const isAssigned = activeNodes.some((n) => n.node_id === dto.node_id);
      if (!isAssigned) {
        throw new BadRequestException(
          `Node #${dto.node_id} is not actively assigned to group #${groupId}.`,
        );
      }

      const cmd = await this.pumpCommandService.sendCommand(
        dto.node_id,
        groupId,
        dto.action,
        treatmentVersionId,
        {
          runLeaseMs: dto.run_lease_ms,
          overrideDurationMs: dto.override_duration_ms,
          source: dto.source,
          gatewayId: deviceIdQuery,
        },
      );
      return [cmd];
    }

    // Fan-out to all active nodes in the group
    const results: PumpCommand[] = [];
    for (const node of activeNodes) {
      const cmd = await this.pumpCommandService.sendCommand(
        node.node_id,
        groupId,
        dto.action,
        treatmentVersionId,
        {
          runLeaseMs: dto.run_lease_ms,
          overrideDurationMs: dto.override_duration_ms,
          source: dto.source,
          gatewayId: deviceIdQuery,
        },
      );
      results.push(cmd);
    }

    return results;
  }

  /**
   * Manual override endpoint for an individual node.
   */
  @Post('node/:nodeId/override')
  @HttpCode(HttpStatus.CREATED)
  async sendNodeOverride(
    @Param('nodeId', ParseIntPipe) nodeId: number,
    @Body() dto: SendPumpCommandDto,
    @Query('deviceId') deviceIdQuery?: string,
  ): Promise<PumpCommand> {
    if (dto.target_type !== PumpTargetType.NODE || dto.group_id !== undefined) {
      throw new BadRequestException('Node commands must target NODE and cannot include group_id.');
    }
    if (dto.node_id !== undefined && dto.node_id !== nodeId) {
      throw new BadRequestException('node_id must match the node route parameter.');
    }
    return this.pumpCommandService.sendCommand(
      nodeId,
      dto.group_id ?? null,
      dto.action,
      null,
      {
        runLeaseMs: dto.run_lease_ms,
        overrideDurationMs: dto.override_duration_ms,
        source: dto.source,
        gatewayId: deviceIdQuery,
      },
    );
  }

  /**
   * S3-F4: Paginate command history for a node (default 50, max 200).
   */
  @Get('node/:nodeId/commands')
  @HttpCode(HttpStatus.OK)
  async getNodeCommands(
    @Param('nodeId', ParseIntPipe) nodeId: number,
    @Query() query: ListNodeCommandsDto,
  ): Promise<PumpCommand[]> {
    return this.pumpCommandService.getNodeCommands(
      nodeId,
      query.limit,
      query.offset,
    );
  }
}

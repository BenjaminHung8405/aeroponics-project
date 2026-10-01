import {
  Controller,
  Get,
  Put,
  Delete,
  Param,
  Body,
  ParseIntPipe,
  Query,
  UseGuards,
  HttpCode,
  HttpStatus,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { GroupService } from './group.service';
import { AssignGroupDto } from './dto/assign-group.dto';
import { GroupStatusResponse } from './group.types';

@Controller('api/group')
@UseGuards(JwtAuthGuard)
export class GroupController {
  constructor(private readonly groupService: GroupService) {}

  @Get()
  @HttpCode(HttpStatus.OK)
  async getAllGroups(): Promise<GroupStatusResponse[]> {
    return this.groupService.getAllGroupsStatus();
  }

  @Get(':id')
  @HttpCode(HttpStatus.OK)
  async getGroupById(
    @Param('id', ParseIntPipe) id: number,
  ): Promise<GroupStatusResponse> {
    return this.groupService.getGroupStatus(id);
  }

  @Put(':id/assign')
  @HttpCode(HttpStatus.OK)
  async assignTreatmentAndNodes(
    @Param('id', ParseIntPipe) id: number,
    @Body() dto: AssignGroupDto,
    @Query('deviceId') deviceId?: string,
  ): Promise<GroupStatusResponse> {
    return deviceId === undefined
      ? this.groupService.assignTreatmentVersion(id, dto)
      : this.groupService.assignTreatmentVersion(id, dto, deviceId);
  }

  @Delete(':id/assign')
  @HttpCode(HttpStatus.OK)
  async unassignGroup(
    @Param('id', ParseIntPipe) id: number,
    @Query('deviceId') deviceId?: string,
  ): Promise<GroupStatusResponse> {
    return deviceId === undefined
      ? this.groupService.unassign(id)
      : this.groupService.unassign(id, deviceId);
  }
}

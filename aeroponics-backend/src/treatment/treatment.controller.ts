import {
  Controller,
  Get,
  Post,
  Put,
  Body,
  Param,
  Query,
  UseGuards,
  ParseIntPipe,
  HttpCode,
  HttpStatus,
} from '@nestjs/common';
import { TreatmentService } from './treatment.service';
import { CreateTreatmentDto } from './dto/create-treatment.dto';
import { CreateTreatmentVersionDto } from './dto/create-treatment-version.dto';
import { CloneTreatmentDto } from './dto/clone-treatment.dto';
import { ListTreatmentDto } from './dto/list-treatment.dto';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { Treatment } from './entities/treatment.entity';
import { TreatmentVersion } from './entities/treatment_version.entity';

@Controller('api/treatment')
@UseGuards(JwtAuthGuard)
export class TreatmentController {
  constructor(private readonly treatmentService: TreatmentService) {}

  /**
   * POST /api/treatment
   * Create a new treatment profile with optional initial timing parameters.
   */
  @Post()
  @HttpCode(HttpStatus.CREATED)
  async create(
    @Body() createTreatmentDto: CreateTreatmentDto,
  ): Promise<Treatment> {
    return this.treatmentService.create(createTreatmentDto);
  }

  /**
   * GET /api/treatment
   * List all treatments with pagination and archive filtering.
   */
  @Get()
  @HttpCode(HttpStatus.OK)
  async list(
    @Query() query: ListTreatmentDto,
  ): Promise<{ items: Treatment[]; total: number }> {
    return this.treatmentService.list(query);
  }

  /**
   * GET /api/treatment/:id
   * Retrieve a treatment by ID including its versions.
   */
  @Get(':id')
  @HttpCode(HttpStatus.OK)
  async getById(
    @Param('id', ParseIntPipe) id: number,
  ): Promise<Treatment> {
    return this.treatmentService.getById(id);
  }

  /**
   * POST /api/treatment/:id/version
   * Add a new version to an existing treatment.
   */
  @Post(':id/version')
  @HttpCode(HttpStatus.CREATED)
  async addVersion(
    @Param('id', ParseIntPipe) id: number,
    @Body() dto: CreateTreatmentVersionDto,
  ): Promise<TreatmentVersion> {
    return this.treatmentService.addVersion(id, dto);
  }

  /**
   * PUT /api/treatment/:id/version/:versionId/publish
   * Publish a draft treatment version, locking it into an immutable state.
   */
  @Put(':id/version/:versionId/publish')
  @HttpCode(HttpStatus.OK)
  async publishVersion(
    @Param('id', ParseIntPipe) id: number,
    @Param('versionId', ParseIntPipe) versionId: number,
  ): Promise<TreatmentVersion> {
    return this.treatmentService.publishVersion(id, versionId);
  }

  /**
   * POST /api/treatment/:id/clone
   * Clone a treatment and its versions (all versions reset to DRAFT).
   */
  @Post(':id/clone')
  @HttpCode(HttpStatus.CREATED)
  async clone(
    @Param('id', ParseIntPipe) id: number,
    @Body() dto: CloneTreatmentDto,
  ): Promise<Treatment> {
    return this.treatmentService.clone(id, dto);
  }

  /**
   * PUT /api/treatment/:id/archive
   * Archive a treatment.
   */
  @Put(':id/archive')
  @HttpCode(HttpStatus.OK)
  async archive(
    @Param('id', ParseIntPipe) id: number,
  ): Promise<Treatment> {
    return this.treatmentService.archive(id);
  }
}

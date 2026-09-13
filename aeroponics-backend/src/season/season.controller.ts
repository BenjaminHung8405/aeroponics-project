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
import { SeasonService } from './season.service';
import { CreateSeasonDto } from './dto/create-season.dto';
import { EndSeasonDto } from './dto/end-season.dto';
import { ListSeasonDto } from './dto/list-season.dto';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { Season } from './entities/season.entity';

@Controller('api/season')
@UseGuards(JwtAuthGuard)
export class SeasonController {
  constructor(private readonly seasonService: SeasonService) {}

  /**
   * POST /api/season
   * Create a new agricultural season.
   */
  @Post()
  @HttpCode(HttpStatus.CREATED)
  async create(@Body() createSeasonDto: CreateSeasonDto): Promise<Season> {
    return this.seasonService.create(createSeasonDto);
  }

  /**
   * GET /api/season/active
   * Retrieve the currently active season, or null if none is active.
   */
  @Get('active')
  @HttpCode(HttpStatus.OK)
  async getActive(): Promise<Season | null> {
    return this.seasonService.getActive();
  }

  /**
   * GET /api/season/:id
   * Retrieve a specific season by its numeric ID.
   */
  @Get(':id')
  @HttpCode(HttpStatus.OK)
  async getById(@Param('id', ParseIntPipe) id: number): Promise<Season> {
    return this.seasonService.getById(id);
  }

  /**
   * PUT /api/season/:id/end
   * End an active season by ID.
   */
  @Put(':id/end')
  @HttpCode(HttpStatus.OK)
  async endSeason(
    @Param('id', ParseIntPipe) id: number,
    @Body() endSeasonDto: EndSeasonDto,
  ): Promise<Season> {
    return this.seasonService.endSeason(id, endSeasonDto);
  }

  /**
   * GET /api/season
   * List seasons with optional status filter and pagination.
   */
  @Get()
  @HttpCode(HttpStatus.OK)
  async list(
    @Query() query: ListSeasonDto,
  ): Promise<{ items: Season[]; total: number }> {
    return this.seasonService.list(query);
  }
}

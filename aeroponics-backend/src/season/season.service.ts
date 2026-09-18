import {
  Injectable,
  ConflictException,
  NotFoundException,
  BadRequestException,
  Logger,
} from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, DataSource, QueryFailedError } from 'typeorm';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { Season, SeasonStatus } from './entities/season.entity';
import { CreateSeasonDto } from './dto/create-season.dto';
import { EndSeasonDto } from './dto/end-season.dto';
import { ListSeasonDto } from './dto/list-season.dto';
import { SeasonCreatedEvent, SeasonEndedEvent } from './events/season.events';

@Injectable()
export class SeasonService {
  private readonly logger = new Logger(SeasonService.name);

  constructor(
    @InjectRepository(Season)
    private readonly seasonRepository: Repository<Season>,
    private readonly dataSource: DataSource,
    private readonly eventEmitter: EventEmitter2,
  ) {}

  /**
   * Create a new season.
   * Enforces the single active season rule both at the application level and DB level.
   */
  async create(dto: CreateSeasonDto): Promise<Season> {
    const activeSeason = await this.getActive();
    if (activeSeason) {
      throw new ConflictException(
        `An active season already exists (ID: ${activeSeason.id}, Name: "${activeSeason.name}"). End the active season before creating a new one.`,
      );
    }

    const season = this.seasonRepository.create({
      name: dto.name,
      notes: dto.notes ?? null,
      status: SeasonStatus.ACTIVE,
      started_at: new Date(),
      target_ec: dto.target_ec ?? null,
      target_ph: dto.target_ph ?? null,
    });

    try {
      const savedSeason = await this.seasonRepository.save(season);
      this.logger.log(
        `Season #${savedSeason.id} ("${savedSeason.name}") created and activated successfully.`,
      );

      this.eventEmitter.emit(
        'season.created',
        new SeasonCreatedEvent(
          savedSeason.id,
          savedSeason.name,
          savedSeason.started_at,
        ),
      );

      return savedSeason;
    } catch (error: unknown) {
      // Catch PostgreSQL unique constraint violation (code 23505 on uq_seasons_one_active)
      if (
        error instanceof QueryFailedError &&
        (error as QueryFailedError & { driverError?: { code?: string } }).driverError?.code === '23505'
      ) {
        this.logger.warn(
          `Concurrent creation blocked by DB unique index uq_seasons_one_active: ${error.message}`,
        );
        throw new ConflictException(
          'An active season already exists. Cannot activate multiple seasons simultaneously.',
        );
      }
      throw error;
    }
  }

  /**
   * Retrieve the currently active season, or null if no season is active.
   */
  async getActive(): Promise<Season | null> {
    return this.seasonRepository.findOne({
      where: { status: SeasonStatus.ACTIVE },
    });
  }

  /**
   * Retrieve a specific season by its ID.
   */
  async getById(id: number): Promise<Season> {
    const season = await this.seasonRepository.findOne({
      where: { id },
    });

    if (!season) {
      throw new NotFoundException(`Season with ID #${id} not found.`);
    }

    return season;
  }

  /**
   * List all seasons with optional filtering by status and pagination.
   */
  async list(
    query?: ListSeasonDto,
  ): Promise<{ items: Season[]; total: number }> {
    const limit = query?.limit ?? 20;
    const offset = query?.offset ?? 0;

    const queryBuilder = this.seasonRepository
      .createQueryBuilder('season')
      .orderBy('season.started_at', 'DESC')
      .addOrderBy('season.id', 'DESC')
      .take(limit)
      .skip(offset);

    if (query?.status) {
      queryBuilder.where('season.status = :status', { status: query.status });
    }

    const [items, total] = await queryBuilder.getManyAndCount();
    return { items, total };
  }

  /**
   * End an active season.
   * Updates status to ENDED, sets ended_at timestamp, and executes inside a database transaction.
   */
  async endSeason(id: number, dto?: EndSeasonDto): Promise<Season> {
    return this.dataSource.transaction(async (manager) => {
      const season = await manager.findOne(Season, {
        where: { id },
        lock: { mode: 'pessimistic_write' },
      });

      if (!season) {
        throw new NotFoundException(`Season with ID #${id} not found.`);
      }

      if (season.status === SeasonStatus.ENDED) {
        throw new BadRequestException(
          `Season #${id} ("${season.name}") is already ended on ${season.ended_at?.toISOString()}.`,
        );
      }

      season.status = SeasonStatus.ENDED;
      season.ended_at = new Date();

      if (dto?.notes) {
        season.notes = season.notes
          ? `${season.notes}\n[End Notes]: ${dto.notes}`
          : dto.notes;
      }

      const updatedSeason = await manager.save(Season, season);
      this.logger.log(
        `Season #${updatedSeason.id} ("${updatedSeason.name}") has been officially ended.`,
      );

      this.eventEmitter.emit(
        'season.ended',
        new SeasonEndedEvent(
          updatedSeason.id,
          updatedSeason.name,
          updatedSeason.ended_at!,
          updatedSeason.notes,
        ),
      );

      return updatedSeason;
    });
  }
}

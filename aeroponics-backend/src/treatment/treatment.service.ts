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
import { Treatment } from './entities/treatment.entity';
import {
  TreatmentVersion,
  TreatmentVersionStatus,
} from './entities/treatment_version.entity';
import { CreateTreatmentDto } from './dto/create-treatment.dto';
import { CreateTreatmentVersionDto } from './dto/create-treatment-version.dto';
import { CloneTreatmentDto } from './dto/clone-treatment.dto';
import { ListTreatmentDto } from './dto/list-treatment.dto';
import {
  TreatmentCreatedEvent,
  TreatmentVersionCreatedEvent,
  TreatmentVersionPublishedEvent,
  TreatmentClonedEvent,
  TreatmentArchivedEvent,
} from './events/treatment.events';

@Injectable()
export class TreatmentService {
  private readonly logger = new Logger(TreatmentService.name);

  constructor(
    @InjectRepository(Treatment)
    private readonly treatmentRepository: Repository<Treatment>,
    @InjectRepository(TreatmentVersion)
    private readonly treatmentVersionRepository: Repository<TreatmentVersion>,
    private readonly dataSource: DataSource,
    private readonly eventEmitter: EventEmitter2,
  ) {}

  /**
   * Create a new treatment.
   * If initial spray/cooldown parameters are provided, version 1 is created as DRAFT.
   */
  async create(dto: CreateTreatmentDto): Promise<Treatment> {
    return this.dataSource.transaction(async (manager) => {
      const treatment = manager.create(Treatment, {
        name: dto.name,
        is_archived: false,
      });

      const savedTreatment = await manager.save(Treatment, treatment);

      const hasInitialVersion =
        dto.spray_day_s !== undefined &&
        dto.cooldown_day_s !== undefined &&
        dto.spray_night_s !== undefined &&
        dto.cooldown_night_s !== undefined;

      if (hasInitialVersion) {
        const initialVersion = manager.create(TreatmentVersion, {
          treatment_id: savedTreatment.id,
          version_num: 1,
          status: TreatmentVersionStatus.DRAFT,
          spray_day_s: dto.spray_day_s,
          cooldown_day_s: dto.cooldown_day_s,
          spray_night_s: dto.spray_night_s,
          cooldown_night_s: dto.cooldown_night_s,
          created_by: dto.created_by ?? null,
          published_at: null,
        });

        const savedVersion = await manager.save(TreatmentVersion, initialVersion);
        savedTreatment.versions = [savedVersion];

        this.eventEmitter.emit(
          'treatment.version.created',
          new TreatmentVersionCreatedEvent(
            savedTreatment.id,
            savedVersion.id,
            savedVersion.version_num,
            savedVersion.status,
          ),
        );
      } else {
        savedTreatment.versions = [];
      }

      this.logger.log(
        `Treatment #${savedTreatment.id} ("${savedTreatment.name}") created successfully.`,
      );

      this.eventEmitter.emit(
        'treatment.created',
        new TreatmentCreatedEvent(
          savedTreatment.id,
          savedTreatment.name,
          savedTreatment.created_at,
        ),
      );

      return savedTreatment;
    });
  }

  /**
   * Retrieve a treatment by ID along with its versions.
   */
  async getById(id: number): Promise<Treatment> {
    const treatment = await this.treatmentRepository
      .createQueryBuilder('treatment')
      .leftJoinAndSelect('treatment.versions', 'versions')
      .where('treatment.id = :id', { id })
      .orderBy('versions.version_num', 'ASC')
      .getOne();

    if (!treatment) {
      throw new NotFoundException(`Treatment with ID #${id} not found.`);
    }

    return treatment;
  }

  /**
   * List all treatments with pagination and optional is_archived filter.
   */
  async list(
    query?: ListTreatmentDto,
  ): Promise<{ items: Treatment[]; total: number }> {
    const limit = query?.limit ?? 20;
    const offset = query?.offset ?? 0;

    const queryBuilder = this.treatmentRepository
      .createQueryBuilder('treatment')
      .leftJoinAndSelect('treatment.versions', 'versions')
      .orderBy('treatment.created_at', 'DESC')
      .addOrderBy('treatment.id', 'DESC')
      .take(limit)
      .skip(offset);

    if (query?.is_archived !== undefined) {
      queryBuilder.where('treatment.is_archived = :isArchived', {
        isArchived: query.is_archived,
      });
    }

    const [items, total] = await queryBuilder.getManyAndCount();
    return { items, total };
  }

  /**
   * Add a new version to an existing treatment.
   * Version numbers increment monotonically. Initial status is always DRAFT.
   */
  async addVersion(
    treatmentId: number,
    dto: CreateTreatmentVersionDto,
  ): Promise<TreatmentVersion> {
    return this.dataSource.transaction(async (manager) => {
      const treatment = await manager.findOne(Treatment, {
        where: { id: treatmentId },
      });

      if (!treatment) {
        throw new NotFoundException(`Treatment with ID #${treatmentId} not found.`);
      }

      if (treatment.is_archived) {
        throw new BadRequestException(
          `Cannot add version to archived Treatment #${treatmentId}.`,
        );
      }

      // Calculate next version number atomically
      const maxVersionRecord = await manager
        .createQueryBuilder(TreatmentVersion, 'tv')
        .select('MAX(tv.version_num)', 'max')
        .where('tv.treatment_id = :treatmentId', { treatmentId })
        .getRawOne<{ max: number | string | null }>();

      const maxVersion = maxVersionRecord?.max
        ? Number(maxVersionRecord.max)
        : 0;
      const nextVersionNum = maxVersion + 1;

      const newVersion = manager.create(TreatmentVersion, {
        treatment_id: treatmentId,
        version_num: nextVersionNum,
        status: TreatmentVersionStatus.DRAFT,
        spray_day_s: dto.spray_day_s,
        cooldown_day_s: dto.cooldown_day_s,
        spray_night_s: dto.spray_night_s,
        cooldown_night_s: dto.cooldown_night_s,
        created_by: dto.created_by ?? null,
        published_at: null,
      });

      try {
        const savedVersion = await manager.save(TreatmentVersion, newVersion);
        this.logger.log(
          `Treatment #${treatmentId}: version #${savedVersion.version_num} (ID: ${savedVersion.id}) added as DRAFT.`,
        );

        this.eventEmitter.emit(
          'treatment.version.created',
          new TreatmentVersionCreatedEvent(
            treatmentId,
            savedVersion.id,
            savedVersion.version_num,
            savedVersion.status,
          ),
        );

        return savedVersion;
      } catch (error: unknown) {
        if (
          error instanceof QueryFailedError &&
          (error as QueryFailedError & { driverError?: { code?: string } })
            .driverError?.code === '23505'
        ) {
          throw new ConflictException(
            `Version ${nextVersionNum} already exists for Treatment #${treatmentId}.`,
          );
        }
        throw error;
      }
    });
  }

  /**
   * Publish a specific treatment version.
   * Enforces immutability: once PUBLISHED, it cannot be republished (returns 409 Conflict).
   * Protected with pessimistic locking against concurrent publish attempts.
   */
  async publishVersion(
    treatmentId: number,
    versionId: number,
  ): Promise<TreatmentVersion> {
    return this.dataSource.transaction(async (manager) => {
      const version = await manager.findOne(TreatmentVersion, {
        where: { id: versionId, treatment_id: treatmentId },
        lock: { mode: 'pessimistic_write' },
      });

      if (!version) {
        throw new NotFoundException(
          `Treatment version #${versionId} for Treatment #${treatmentId} not found.`,
        );
      }

      if (version.status === TreatmentVersionStatus.PUBLISHED) {
        throw new ConflictException(
          `Treatment version #${versionId} is already published on ${version.published_at?.toISOString()}. Republishing is prohibited.`,
        );
      }

      if (version.status === TreatmentVersionStatus.ARCHIVED) {
        throw new BadRequestException(
          `Treatment version #${versionId} is ARCHIVED and cannot be published.`,
        );
      }

      version.status = TreatmentVersionStatus.PUBLISHED;
      version.published_at = new Date();

      const updatedVersion = await manager.save(TreatmentVersion, version);

      this.logger.log(
        `Treatment #${treatmentId}: version #${updatedVersion.version_num} (ID: ${updatedVersion.id}) published successfully.`,
      );

      this.eventEmitter.emit(
        'treatment.version.published',
        new TreatmentVersionPublishedEvent(
          treatmentId,
          updatedVersion.id,
          updatedVersion.version_num,
          updatedVersion.spray_day_s,
          updatedVersion.cooldown_day_s,
          updatedVersion.spray_night_s,
          updatedVersion.cooldown_night_s,
          updatedVersion.published_at!,
        ),
      );

      return updatedVersion;
    });
  }

  /**
   * Clone a treatment and all its versions.
   * Cloned versions are ALWAYS reset to DRAFT with published_at = null.
   */
  async clone(treatmentId: number, dto?: CloneTreatmentDto): Promise<Treatment> {
    return this.dataSource.transaction(async (manager) => {
      const original = await manager.findOne(Treatment, {
        where: { id: treatmentId },
        relations: ['versions'],
      });

      if (!original) {
        throw new NotFoundException(`Treatment with ID #${treatmentId} not found.`);
      }

      const cloneName = dto?.name ? dto.name : `${original.name} (Copy)`;

      const newTreatment = manager.create(Treatment, {
        name: cloneName,
        is_archived: false,
      });

      const savedTreatment = await manager.save(Treatment, newTreatment);

      const clonedVersions: TreatmentVersion[] = [];
      if (original.versions && original.versions.length > 0) {
        // Sort versions by version_num ascending
        const sortedOriginalVersions = [...original.versions].sort(
          (a, b) => a.version_num - b.version_num,
        );

        for (const v of sortedOriginalVersions) {
          const clonedV = manager.create(TreatmentVersion, {
            treatment_id: savedTreatment.id,
            version_num: v.version_num,
            status: TreatmentVersionStatus.DRAFT, // Always reset to DRAFT!
            published_at: null,                   // Always reset to null!
            spray_day_s: v.spray_day_s,
            cooldown_day_s: v.cooldown_day_s,
            spray_night_s: v.spray_night_s,
            cooldown_night_s: v.cooldown_night_s,
            created_by: v.created_by,
          });
          const savedV = await manager.save(TreatmentVersion, clonedV);
          clonedVersions.push(savedV);
        }
      }

      savedTreatment.versions = clonedVersions;

      this.logger.log(
        `Cloned Treatment #${treatmentId} ("${original.name}") -> New Treatment #${savedTreatment.id} ("${savedTreatment.name}") with ${clonedVersions.length} draft version(s).`,
      );

      this.eventEmitter.emit(
        'treatment.cloned',
        new TreatmentClonedEvent(treatmentId, savedTreatment.id, savedTreatment.name),
      );

      return savedTreatment;
    });
  }

  /**
   * Archive a treatment.
   */
  async archive(treatmentId: number): Promise<Treatment> {
    const treatment = await this.treatmentRepository.findOne({
      where: { id: treatmentId },
      relations: ['versions'],
    });

    if (!treatment) {
      throw new NotFoundException(`Treatment with ID #${treatmentId} not found.`);
    }

    if (treatment.is_archived) {
      return treatment;
    }

    treatment.is_archived = true;
    const savedTreatment = await this.treatmentRepository.save(treatment);

    this.logger.log(`Treatment #${treatmentId} ("${treatment.name}") has been archived.`);

    this.eventEmitter.emit(
      'treatment.archived',
      new TreatmentArchivedEvent(treatmentId, new Date()),
    );

    return savedTreatment;
  }
}

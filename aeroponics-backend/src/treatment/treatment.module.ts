import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { Treatment } from './entities/treatment.entity';
import { TreatmentVersion } from './entities/treatment_version.entity';
import { TreatmentService } from './treatment.service';
import { TreatmentController } from './treatment.controller';
import { AuthModule } from '../auth/auth.module';

@Module({
  imports: [
    TypeOrmModule.forFeature([Treatment, TreatmentVersion]),
    AuthModule,
  ],
  controllers: [TreatmentController],
  providers: [TreatmentService],
  exports: [TreatmentService],
})
export class TreatmentModule {}

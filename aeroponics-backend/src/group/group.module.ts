import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { TimerGroup } from './entities/timer_group.entity';
import { GroupTreatmentAssignment } from './entities/group_treatment_assignment.entity';
import { GroupNodeAssignment } from './entities/group_node_assignment.entity';
import { TreatmentVersion } from '../treatment/entities/treatment_version.entity';
import { NodeRegistry } from '../node/entities/node_registry.entity';
import { SeasonModule } from '../season/season.module';
import { AuthModule } from '../auth/auth.module';
import { GroupService } from './group.service';
import { GroupController } from './group.controller';

@Module({
  imports: [
    TypeOrmModule.forFeature([
      TimerGroup,
      GroupTreatmentAssignment,
      GroupNodeAssignment,
      TreatmentVersion,
      NodeRegistry,
    ]),
    SeasonModule,
    AuthModule,
  ],
  controllers: [GroupController],
  providers: [GroupService],
  exports: [GroupService],
})
export class GroupModule {}

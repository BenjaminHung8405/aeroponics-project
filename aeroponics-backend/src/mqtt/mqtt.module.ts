import { Global, Module, forwardRef } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { EventEmitterModule } from '@nestjs/event-emitter';
import { TypeOrmModule } from '@nestjs/typeorm';

import { MqttService } from './mqtt.service';
import { MqttRouterService } from './mqtt-router.service';
import { ClockSyncService } from './clock-sync.service';
import { GroupScheduleSyncService } from './group-schedule-sync.service';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { TreatmentVersion } from '../treatment/entities/treatment_version.entity';
import { GroupTreatmentAssignment } from '../group/entities/group_treatment_assignment.entity';
import { GroupNodeAssignment } from '../group/entities/group_node_assignment.entity';
import { TimerGroup } from '../group/entities/timer_group.entity';
import { NodeModule } from '../node/node.module';
import { FlowModule } from '../flow/flow.module';
import { PumpCommandModule } from '../pump-command/pump-command.module';

@Global()
@Module({
  imports: [
    ConfigModule,
    EventEmitterModule,
    TypeOrmModule.forFeature([
      DeviceStatus,
      TreatmentVersion,
      GroupTreatmentAssignment,
      GroupNodeAssignment,
      TimerGroup,
    ]),
    NodeModule,
    FlowModule,
    forwardRef(() => PumpCommandModule),
  ],
  providers: [
    MqttService,
    MqttRouterService,
    ClockSyncService,
    GroupScheduleSyncService,
  ],
  exports: [
    MqttService,
    MqttRouterService,
    ClockSyncService,
    GroupScheduleSyncService,
  ],
})
export class MqttModule {}

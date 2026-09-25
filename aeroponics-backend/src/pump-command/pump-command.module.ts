import { Module, forwardRef } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';

import { PumpCommand } from './entities/pump_command.entity';
import { PumpFeedbackEvent } from './entities/pump_feedback_event.entity';
import { PumpStateEvent } from './entities/pump_state_event.entity';
import { FlowEvent } from '../flow/entities/flow_event.entity';
import { SensorCalibration } from '../node/entities/sensor_calibration.entity';
import { NodeRegistry } from '../node/entities/node_registry.entity';
import { SeasonModule } from '../season/season.module';
import { GroupModule } from '../group/group.module';
import { MqttModule } from '../mqtt/mqtt.module';
import { AuthModule } from '../auth/auth.module';
import { PumpCommandService } from './pump-command.service';
import { PumpCommandController } from './pump-command.controller';

@Module({
  imports: [
    TypeOrmModule.forFeature([
      PumpCommand,
      PumpFeedbackEvent,
      PumpStateEvent,
      FlowEvent,
      SensorCalibration,
      NodeRegistry,
    ]),
    SeasonModule,
    GroupModule,
    forwardRef(() => MqttModule),
    AuthModule,
  ],
  controllers: [PumpCommandController],
  providers: [PumpCommandService],
  exports: [PumpCommandService],
})
export class PumpCommandModule {}

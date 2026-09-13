import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { FlowEvent } from './entities/flow_event.entity';
import { SensorCalibration } from '../node/entities/sensor_calibration.entity';
import { NodeRegistry } from '../node/entities/node_registry.entity';
import { Season } from '../season/entities/season.entity';
import { AuthModule } from '../auth/auth.module';
import { FlowService } from './flow.service';
import { FlowController } from './flow.controller';

@Module({
  imports: [
    TypeOrmModule.forFeature([
      FlowEvent,
      SensorCalibration,
      NodeRegistry,
      Season,
    ]),
    AuthModule,
  ],
  controllers: [FlowController],
  providers: [FlowService],
  exports: [FlowService],
})
export class FlowModule {}

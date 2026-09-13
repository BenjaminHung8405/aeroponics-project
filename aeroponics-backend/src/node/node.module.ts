import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { NodeRegistry } from './entities/node_registry.entity';
import { SensorCalibration } from './entities/sensor_calibration.entity';
import { AuthModule } from '../auth/auth.module';
import { AppConfigModule } from '../config/app-config.module';
import { NodeService } from './node.service';
import { NodeController } from './node.controller';

@Module({
  imports: [
    TypeOrmModule.forFeature([NodeRegistry, SensorCalibration]),
    AuthModule,
    AppConfigModule,
  ],
  controllers: [NodeController],
  providers: [NodeService],
  exports: [NodeService],
})
export class NodeModule {}

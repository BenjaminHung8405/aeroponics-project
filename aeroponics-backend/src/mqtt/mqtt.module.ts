import { Global, Module, forwardRef } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { EventEmitterModule } from '@nestjs/event-emitter';
import { TypeOrmModule } from '@nestjs/typeorm';

import { MqttService } from './mqtt.service';
import { MqttRouterService } from './mqtt-router.service';
import { DeviceStatus } from '../device/entities/device_status.entity';
import { NodeModule } from '../node/node.module';
import { FlowModule } from '../flow/flow.module';
import { PumpCommandModule } from '../pump-command/pump-command.module';

@Global()
@Module({
  imports: [
    ConfigModule,
    EventEmitterModule,
    TypeOrmModule.forFeature([DeviceStatus]),
    NodeModule,
    FlowModule,
    forwardRef(() => PumpCommandModule),
  ],
  providers: [MqttService, MqttRouterService],
  exports: [MqttService, MqttRouterService],
})
export class MqttModule {}

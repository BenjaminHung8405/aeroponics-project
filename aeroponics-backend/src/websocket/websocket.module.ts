import { Module } from '@nestjs/common';
import { EventsGateway } from './events.gateway';
import { NodeModule } from '../node/node.module';
import { GroupModule } from '../group/group.module';
import { DeviceModule } from '../device/device.module';

@Module({
  imports: [NodeModule, GroupModule, DeviceModule],
  providers: [EventsGateway],
  exports: [EventsGateway],
})
export class WebsocketModule {}

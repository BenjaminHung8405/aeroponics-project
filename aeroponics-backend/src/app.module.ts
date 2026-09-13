import { Module } from '@nestjs/common';
import { EventEmitterModule } from '@nestjs/event-emitter';
import { ServeStaticModule } from '@nestjs/serve-static';
import { join } from 'path';
import { AppConfigModule } from './config/app-config.module';
import { DatabaseModule } from './database/database.module';
import { AuthModule } from './auth/auth.module';
import { MqttModule } from './mqtt/mqtt.module';
import { SeasonModule } from './season/season.module';
import { TreatmentModule } from './treatment/treatment.module';
import { GroupModule } from './group/group.module';
import { NodeModule } from './node/node.module';
import { PumpCommandModule } from './pump-command/pump-command.module';
import { FlowModule } from './flow/flow.module';
import { TuyaBridgeModule } from './tuya-bridge/tuya-bridge.module';
import { WebsocketModule } from './websocket/websocket.module';
import { DeviceModule } from './device/device.module';
import { AppController } from './app.controller';

@Module({
  imports: [
    AppConfigModule,
    EventEmitterModule.forRoot(),
    ServeStaticModule.forRoot({
      rootPath: join(__dirname, '..', 'public'),
      exclude: ['/api/(.*)'],
    }),
    DatabaseModule,
    AuthModule,
    MqttModule,
    SeasonModule,
    TreatmentModule,
    GroupModule,
    NodeModule,
    PumpCommandModule,
    FlowModule,
    TuyaBridgeModule,
    WebsocketModule,
    DeviceModule,
  ],
  controllers: [AppController],
})
export class AppModule {}


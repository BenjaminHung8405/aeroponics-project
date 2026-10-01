import { Module, forwardRef } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { AuthModule } from '../auth/auth.module';
import { DeviceModule } from '../device/device.module';
import { MqttModule } from '../mqtt/mqtt.module';
import { ControlSlotController } from './control-slot.controller';
import { ControlSlotService } from './control-slot.service';
import { ControlSlot } from './entities/control_slot.entity';

@Module({
  imports: [
    TypeOrmModule.forFeature([ControlSlot]),
    AuthModule,
    forwardRef(() => MqttModule),
    forwardRef(() => DeviceModule),
  ],
  controllers: [ControlSlotController],
  providers: [ControlSlotService],
  exports: [ControlSlotService],
})
export class ControlSlotModule {}

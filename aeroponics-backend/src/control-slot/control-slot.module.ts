import { Module, forwardRef } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { AuthModule } from '../auth/auth.module';
import { MqttModule } from '../mqtt/mqtt.module';
import { DeviceModule } from '../device/device.module';
import { ControlSlot } from './entities/control_slot.entity';
import { ControlSlotController } from './control-slot.controller';
import { ControlSlotService } from './control-slot.service';

@Module({
  imports: [
    TypeOrmModule.forFeature([ControlSlot]),
    AuthModule,
    MqttModule,
    forwardRef(() => DeviceModule),
  ],
  controllers: [ControlSlotController],
  providers: [ControlSlotService],
  exports: [ControlSlotService],
})
export class ControlSlotModule {}


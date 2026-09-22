import { Module, forwardRef } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { Device } from './entities/device.entity';
import { DeviceStatus } from './entities/device_status.entity';
import { DeviceService } from './device.service';
import { DeviceController } from './device.controller';
import { AuthModule } from '../auth/auth.module';
import { NodeModule } from '../node/node.module';

@Module({
  imports: [
    TypeOrmModule.forFeature([Device, DeviceStatus]),
    AuthModule,
    forwardRef(() => NodeModule),
  ],
  controllers: [DeviceController],
  providers: [DeviceService],
  exports: [DeviceService],
})
export class DeviceModule {}

import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { TuyaMeasurementSession } from './entities/tuya_measurement_session.entity';
import { MeasurementReading } from './entities/measurement_reading.entity';
import { TuyaBridgeService } from './tuya-bridge.service';
import { TuyaBridgeController } from './tuya-bridge.controller';
import { SeasonModule } from '../season/season.module';
import { AuthModule } from '../auth/auth.module';

@Module({
  imports: [
    TypeOrmModule.forFeature([TuyaMeasurementSession, MeasurementReading]),
    SeasonModule,
    AuthModule,
  ],
  controllers: [TuyaBridgeController],
  providers: [TuyaBridgeService],
  exports: [TuyaBridgeService],
})
export class TuyaBridgeModule {}

import { NestFactory } from '@nestjs/core';
import { AppModule } from './app.module';
import { ValidationPipe } from '@nestjs/common';
import { NestExpressApplication } from '@nestjs/platform-express';
import { WsAdapter } from '@nestjs/platform-ws';

async function bootstrap() {
  const app = await NestFactory.create<NestExpressApplication>(AppModule);

  // Security Rules: Hide X-Powered-By header
  app.disable('x-powered-by');

  // Security Rules: Controlled CORS configuration with credentials support
  const corsOrigin = process.env.CORS_ORIGIN;
  app.enableCors({
    origin: corsOrigin ? corsOrigin.split(',').map((o) => o.trim()) : true,
    methods: 'GET,HEAD,PUT,PATCH,POST,DELETE,OPTIONS',
    credentials: true,
  });

  // Global Validation Pipe
  app.useGlobalPipes(
    new ValidationPipe({
      whitelist: true,
      transform: true,
      forbidNonWhitelisted: true,
    }),
  );

  // Native WebSocket Adapter (Hard Rule S3-WS-NATIVE-06)
  app.useWebSocketAdapter(new WsAdapter(app));

  const port = process.env.PORT || 3001;
  await app.listen(port);
  console.log(`Aeroponics Backend service started successfully on port ${port}`);
}

bootstrap();

import { Test, TestingModule } from '@nestjs/testing';
import {
  INestApplication,
  ValidationPipe,
  BadRequestException,
  ConflictException,
  NotFoundException,
  HttpException,
  HttpStatus,
} from '@nestjs/common';
import * as request from 'supertest';
import { JwtModule, JwtService } from '@nestjs/jwt';
import { PassportModule } from '@nestjs/passport';
import { ConfigModule } from '@nestjs/config';
import { DataSource } from 'typeorm';

import { AppController } from '../src/app.controller';
import { AuthController } from '../src/auth/auth.controller';
import { AuthService } from '../src/auth/auth.service';
import { JwtStrategy } from '../src/auth/jwt.strategy';
import { JwtAuthGuard } from '../src/auth/jwt-auth.guard';

import { SeasonController } from '../src/season/season.controller';
import { SeasonService } from '../src/season/season.service';
import { TreatmentController } from '../src/treatment/treatment.controller';
import { TreatmentService } from '../src/treatment/treatment.service';
import { GroupController } from '../src/group/group.controller';
import { GroupService } from '../src/group/group.service';
import { NodeController } from '../src/node/node.controller';
import { NodeService } from '../src/node/node.service';
import { PumpCommandController } from '../src/pump-command/pump-command.controller';
import { PumpCommandService } from '../src/pump-command/pump-command.service';
import { FlowController } from '../src/flow/flow.controller';
import { FlowService } from '../src/flow/flow.service';
import { TuyaBridgeController } from '../src/tuya-bridge/tuya-bridge.controller';
import { TuyaBridgeService } from '../src/tuya-bridge/tuya-bridge.service';
import { DeviceController } from '../src/device/device.controller';
import { DeviceService } from '../src/device/device.service';

describe('RestApi E2E Validation (Sprint 3 — 22+ REST Endpoints)', () => {
  let app: INestApplication;
  let jwtService: JwtService;
  let validToken: string;

  // Mock service references
  let mockDataSource: { isInitialized: boolean; query: jest.Mock };
  let mockSeasonService: Record<string, jest.Mock>;
  let mockTreatmentService: Record<string, jest.Mock>;
  let mockGroupService: Record<string, jest.Mock>;
  let mockNodeService: Record<string, jest.Mock>;
  let mockPumpCommandService: Record<string, jest.Mock>;
  let mockFlowService: Record<string, jest.Mock>;
  let mockTuyaBridgeService: Record<string, jest.Mock>;
  let mockDeviceService: Record<string, jest.Mock>;

  const TEST_JWT_SECRET = 'e2e_test_super_secret_jwt_key_32chars_min';

  beforeAll(async () => {
    mockDataSource = {
      isInitialized: true,
      query: jest.fn().mockResolvedValue([{ '?column?': 1 }]),
    };

    mockSeasonService = {
      create: jest.fn(),
      getActive: jest.fn(),
      getById: jest.fn(),
      list: jest.fn(),
      endSeason: jest.fn(),
    };

    mockTreatmentService = {
      create: jest.fn(),
      addVersion: jest.fn(),
      publishVersion: jest.fn(),
      clone: jest.fn(),
      archive: jest.fn(),
      getById: jest.fn(),
      list: jest.fn(),
    };

    mockGroupService = {
      getGroupStatus: jest.fn(),
      getAllGroupsStatus: jest.fn(),
      assignTreatmentVersion: jest.fn(),
      unassign: jest.fn(),
    };

    mockNodeService = {
      getNodeStatus: jest.fn(),
      getAllNodesStatus: jest.fn(),
      updateCalibration: jest.fn(),
      resetFault: jest.fn(),
    };

    mockPumpCommandService = {
      sendCommand: jest.fn(),
      getNodeCommands: jest.fn(),
    };

    mockFlowService = {
      getHistory: jest.fn(),
      getCalibration: jest.fn(),
      updateCalibration: jest.fn(),
    };

    mockTuyaBridgeService = {
      measureOnDemand: jest.fn(),
      getLatest: jest.fn(),
      getHistory: jest.fn(),
    };

    mockDeviceService = {
      getDeviceStatus: jest.fn(),
    };

    const moduleFixture: TestingModule = await Test.createTestingModule({
      imports: [
        PassportModule.register({ defaultStrategy: 'jwt' }),
        JwtModule.register({
          secret: TEST_JWT_SECRET,
          signOptions: { expiresIn: '1h' },
        }),
        ConfigModule.forRoot({
          isGlobal: true,
          load: [
            () => ({
              JWT_SECRET: TEST_JWT_SECRET,
              ADMIN_USERNAME: 'admin',
              ADMIN_PASSWORD: '123456',
            }),
          ],
        }),
      ],
      controllers: [
        AppController,
        AuthController,
        SeasonController,
        TreatmentController,
        GroupController,
        NodeController,
        PumpCommandController,
        FlowController,
        TuyaBridgeController,
        DeviceController,
      ],
      providers: [
        AuthService,
        JwtStrategy,
        JwtAuthGuard,
        { provide: DataSource, useValue: mockDataSource },
        { provide: SeasonService, useValue: mockSeasonService },
        { provide: TreatmentService, useValue: mockTreatmentService },
        { provide: GroupService, useValue: mockGroupService },
        { provide: NodeService, useValue: mockNodeService },
        { provide: PumpCommandService, useValue: mockPumpCommandService },
        { provide: FlowService, useValue: mockFlowService },
        { provide: TuyaBridgeService, useValue: mockTuyaBridgeService },
        { provide: DeviceService, useValue: mockDeviceService },
      ],
    }).compile();

    app = moduleFixture.createNestApplication();
    app.useGlobalPipes(
      new ValidationPipe({
        whitelist: true,
        transform: true,
        forbidNonWhitelisted: true,
      }),
    );

    await app.init();

    jwtService = moduleFixture.get<JwtService>(JwtService);
    validToken = jwtService.sign({
      sub: 'operator_01',
      username: 'operator',
      role: 'admin',
    });
  });

  afterAll(async () => {
    await app.close();
  });

  beforeEach(() => {
    jest.clearAllMocks();
    mockDataSource.isInitialized = true;
    mockDataSource.query.mockResolvedValue([{ '?column?': 1 }]);
  });

  // =========================================================================
  // 1. SYSTEM & HEALTH ENDPOINTS
  // =========================================================================
  describe('1. System & Health Check Endpoints', () => {
    it('GET /health (Success) -> 200 with { status: "ok", db: "connected" }', async () => {
      const res = await request(app.getHttpServer()).get('/health');
      expect(res.status).toBe(200);
      expect(res.body).toEqual({ status: 'ok', db: 'connected' });
      expect(mockDataSource.query).toHaveBeenCalledWith('SELECT 1');
    });

    it('GET /health (DB Down) -> 503 Service Unavailable', async () => {
      mockDataSource.query.mockRejectedValue(new Error('Connection lost'));
      const res = await request(app.getHttpServer()).get('/health');
      expect(res.status).toBe(503);
      expect(res.body.db).toBe('disconnected');
    });
  });

  // =========================================================================
  // 2. AUTHENTICATION ENDPOINTS
  // =========================================================================
  describe('2. Authentication (POST /api/auth/login)', () => {
    it('Success case (200) -> returns access_token', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/auth/login')
        .send({ username: 'admin', password: '123456' });
      expect(res.status).toBe(200);
      expect(res.body).toHaveProperty('access_token');
    });

    it('Auth fail case (401) -> invalid credentials', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/auth/login')
        .send({ username: 'admin', password: 'wrong_password' });
      expect(res.status).toBe(401);
    });

    it('Invalid input case (400) -> missing username or password', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/auth/login')
        .send({ username: '' });
      expect(res.status).toBe(400);
    });
  });

  // =========================================================================
  // 3. SEASON ENDPOINTS
  // =========================================================================
  describe('3. Season Module Endpoints', () => {
    it('POST /api/season (Success) -> 201 Created', async () => {
      const seasonObj = { id: 1, name: 'Vụ Cà Chua Q3', status: 'ACTIVE' };
      mockSeasonService.create.mockResolvedValue(seasonObj);

      const res = await request(app.getHttpServer())
        .post('/api/season')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ name: 'Vụ Cà Chua Q3', notes: 'Giai đoạn thử nghiệm' });

      expect(res.status).toBe(201);
      expect(res.body).toEqual(seasonObj);
    });

    it('POST /api/season (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/season')
        .send({ name: 'Vụ Cà Chua Q3' });
      expect(res.status).toBe(401);
    });

    it('POST /api/season (Invalid Input) -> 400 Bad Request (empty name)', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/season')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ name: '   ' });
      expect(res.status).toBe(400);
    });

    it('POST /api/season (Conflict) -> 409 Conflict (active season exists)', async () => {
      mockSeasonService.create.mockRejectedValue(
        new ConflictException('An active season already exists'),
      );

      const res = await request(app.getHttpServer())
        .post('/api/season')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ name: 'Vụ Trùng' });
      expect(res.status).toBe(409);
    });

    it('GET /api/season/active (Success) -> 200 OK', async () => {
      mockSeasonService.getActive.mockResolvedValue({ id: 1, name: 'Vụ Cà Chua Q3' });

      const res = await request(app.getHttpServer())
        .get('/api/season/active')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body).toEqual({ id: 1, name: 'Vụ Cà Chua Q3' });
    });

    it('GET /api/season/active (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/season/active');
      expect(res.status).toBe(401);
    });

    it('PUT /api/season/:id/end (Success) -> 200 OK', async () => {
      mockSeasonService.endSeason.mockResolvedValue({ id: 1, status: 'ENDED' });

      const res = await request(app.getHttpServer())
        .put('/api/season/1/end')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ notes: 'Kết thúc vụ mùa đạt năng suất' });
      expect(res.status).toBe(200);
      expect(res.body).toEqual({ id: 1, status: 'ENDED' });
    });

    it('PUT /api/season/:id/end (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).put('/api/season/1/end');
      expect(res.status).toBe(401);
    });

    it('PUT /api/season/:id/end (Bad Request) -> 400 when already ended', async () => {
      mockSeasonService.endSeason.mockRejectedValue(
        new BadRequestException('Season 1 is already ENDED'),
      );

      const res = await request(app.getHttpServer())
        .put('/api/season/1/end')
        .set('Authorization', `Bearer ${validToken}`)
        .send({});
      expect(res.status).toBe(400);
    });

    it('GET /api/season (Success) -> 200 OK', async () => {
      mockSeasonService.list.mockResolvedValue([{ id: 1, name: 'Vụ 1' }]);

      const res = await request(app.getHttpServer())
        .get('/api/season?limit=10&offset=0')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(Array.isArray(res.body)).toBe(true);
    });

    it('GET /api/season (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/season');
      expect(res.status).toBe(401);
    });

    it('GET /api/season (Invalid Query) -> 400 Bad Request', async () => {
      const res = await request(app.getHttpServer())
        .get('/api/season?limit=999')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });
  });

  // =========================================================================
  // 4. TREATMENT ENDPOINTS
  // =========================================================================
  describe('4. Treatment Module Endpoints', () => {
    it('POST /api/treatment (Success) -> 201 Created', async () => {
      mockTreatmentService.create.mockResolvedValue({ id: 1, name: 'Công thức Dâu tây' });

      const res = await request(app.getHttpServer())
        .post('/api/treatment')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ name: 'Công thức Dâu tây' });
      expect(res.status).toBe(201);
      expect(res.body.name).toBe('Công thức Dâu tây');
    });

    it('POST /api/treatment (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/treatment')
        .send({ name: 'Công thức Dâu tây' });
      expect(res.status).toBe(401);
    });

    it('POST /api/treatment (Invalid Input) -> 400 Bad Request (empty name)', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/treatment')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ name: '' });
      expect(res.status).toBe(400);
    });

    it('POST /api/treatment/:id/version (Success) -> 201 Created', async () => {
      mockTreatmentService.addVersion.mockResolvedValue({
        id: 2,
        treatment_id: 1,
        version_num: 2,
        spray_day_s: 15,
        cooldown_day_s: 300,
        spray_night_s: 10,
        cooldown_night_s: 600,
      });

      const res = await request(app.getHttpServer())
        .post('/api/treatment/1/version')
        .set('Authorization', `Bearer ${validToken}`)
        .send({
          spray_day_s: 15,
          cooldown_day_s: 300,
          spray_night_s: 10,
          cooldown_night_s: 600,
        });
      expect(res.status).toBe(201);
      expect(res.body.version_num).toBe(2);
    });

    it('POST /api/treatment/:id/version (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/treatment/1/version')
        .send({ spray_day_s: 15, cooldown_day_s: 300, spray_night_s: 10, cooldown_night_s: 600 });
      expect(res.status).toBe(401);
    });

    it('POST /api/treatment/:id/version (Invalid Bounds) -> 400 Bad Request (spray_day_s out of bounds)', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/treatment/1/version')
        .set('Authorization', `Bearer ${validToken}`)
        .send({
          spray_day_s: 9999,
          cooldown_day_s: 300,
          spray_night_s: 10,
          cooldown_night_s: 600,
        });
      expect(res.status).toBe(400);
    });

    it('PUT /api/treatment/:id/version/:versionId/publish (Success) -> 200 OK', async () => {
      mockTreatmentService.publishVersion.mockResolvedValue({
        id: 2,
        status: 'PUBLISHED',
      });

      const res = await request(app.getHttpServer())
        .put('/api/treatment/1/version/2/publish')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.status).toBe('PUBLISHED');
    });

    it('PUT /api/treatment/:id/version/:versionId/publish (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).put('/api/treatment/1/version/2/publish');
      expect(res.status).toBe(401);
    });

    it('PUT /api/treatment/:id/version/:versionId/publish (Conflict) -> 409 Conflict when already published', async () => {
      mockTreatmentService.publishVersion.mockRejectedValue(
        new ConflictException('Treatment version 2 is already PUBLISHED'),
      );

      const res = await request(app.getHttpServer())
        .put('/api/treatment/1/version/2/publish')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(409);
    });

    it('POST /api/treatment/:id/clone (Success) -> 201 Created', async () => {
      mockTreatmentService.clone.mockResolvedValue({
        id: 3,
        name: 'Công thức Dâu tây Clone',
      });

      const res = await request(app.getHttpServer())
        .post('/api/treatment/1/clone')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ name: 'Công thức Dâu tây Clone' });
      expect(res.status).toBe(201);
      expect(res.body.name).toBe('Công thức Dâu tây Clone');
    });

    it('POST /api/treatment/:id/clone (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/treatment/1/clone')
        .send({ name: 'Clone' });
      expect(res.status).toBe(401);
    });

    it('POST /api/treatment/:id/clone (Invalid Input) -> 400 Bad Request (name exceeds 100 chars)', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/treatment/1/clone')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ name: 'a'.repeat(105) });
      expect(res.status).toBe(400);
    });

    it('PUT /api/treatment/:id/archive (Success) -> 200 OK', async () => {
      mockTreatmentService.archive.mockResolvedValue({ id: 1, is_archived: true });

      const res = await request(app.getHttpServer())
        .put('/api/treatment/1/archive')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.is_archived).toBe(true);
    });

    it('PUT /api/treatment/:id/archive (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).put('/api/treatment/1/archive');
      expect(res.status).toBe(401);
    });

    it('GET /api/treatment (Success) -> 200 OK', async () => {
      mockTreatmentService.list.mockResolvedValue([{ id: 1, name: 'Công thức 1' }]);

      const res = await request(app.getHttpServer())
        .get('/api/treatment')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(Array.isArray(res.body)).toBe(true);
    });

    it('GET /api/treatment (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/treatment');
      expect(res.status).toBe(401);
    });
  });

  // =========================================================================
  // 5. GROUP ENDPOINTS
  // =========================================================================
  describe('5. Group Module Endpoints', () => {
    it('GET /api/group (Success) -> 200 OK', async () => {
      mockGroupService.getAllGroupsStatus.mockResolvedValue([
        { groupId: 1, status: 'ACTIVE' },
        { groupId: 2, status: 'UNASSIGNED' },
      ]);

      const res = await request(app.getHttpServer())
        .get('/api/group')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.length).toBe(2);
    });

    it('GET /api/group (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/group');
      expect(res.status).toBe(401);
    });

    it('GET /api/group/:id (Success) -> 200 OK', async () => {
      mockGroupService.getGroupStatus.mockResolvedValue({ groupId: 1, phase: 'DAY' });

      const res = await request(app.getHttpServer())
        .get('/api/group/1')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.groupId).toBe(1);
    });

    it('GET /api/group/:id (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/group/1');
      expect(res.status).toBe(401);
    });

    it('GET /api/group/:id (Invalid Param) -> 400 Bad Request (non-numeric id)', async () => {
      const res = await request(app.getHttpServer())
        .get('/api/group/invalid-param')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });

    it('PUT /api/group/:id/assign (Success) -> 200 OK', async () => {
      mockGroupService.assignTreatmentVersion.mockResolvedValue({
        groupId: 1,
        status: 'ACTIVE',
        nodeIds: [1, 2],
      });

      const res = await request(app.getHttpServer())
        .put('/api/group/1/assign')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ treatment_version_id: 10, node_ids: [1, 2] });
      expect(res.status).toBe(200);
      expect(res.body.status).toBe('ACTIVE');
    });

    it('PUT /api/group/:id/assign (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .put('/api/group/1/assign')
        .send({ treatment_version_id: 10, node_ids: [1, 2] });
      expect(res.status).toBe(401);
    });

    it('PUT /api/group/:id/assign (Conflict) -> 409 Conflict (node already assigned)', async () => {
      mockGroupService.assignTreatmentVersion.mockRejectedValue(
        new ConflictException('Node #1 is already active in Group #2'),
      );

      const res = await request(app.getHttpServer())
        .put('/api/group/1/assign')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ treatment_version_id: 10, node_ids: [1] });
      expect(res.status).toBe(409);
    });

    it('PUT /api/group/:id/assign (Invalid Input) -> 400 Bad Request (node_ids empty or outside 1..4)', async () => {
      const res = await request(app.getHttpServer())
        .put('/api/group/1/assign')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ treatment_version_id: 10, node_ids: [99] });
      expect(res.status).toBe(400);
    });

    it('DELETE /api/group/:id/assign (Success) -> 200 OK', async () => {
      mockGroupService.unassign.mockResolvedValue({ groupId: 1, status: 'UNASSIGNED' });

      const res = await request(app.getHttpServer())
        .delete('/api/group/1/assign')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.status).toBe('UNASSIGNED');
    });

    it('DELETE /api/group/:id/assign (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).delete('/api/group/1/assign');
      expect(res.status).toBe(401);
    });

    it('DELETE /api/group/:id/assign (Invalid Param) -> 400 Bad Request (non-numeric id)', async () => {
      const res = await request(app.getHttpServer())
        .delete('/api/group/invalid-id/assign')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });
  });

  // =========================================================================
  // 6. NODE ENDPOINTS
  // =========================================================================
  describe('6. Node Module Endpoints', () => {
    it('GET /api/node (Success) -> 200 OK', async () => {
      mockNodeService.getAllNodesStatus.mockResolvedValue([
        { nodeId: 1, health: 'OK' },
        { nodeId: 2, health: 'OK' },
      ]);

      const res = await request(app.getHttpServer())
        .get('/api/node')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.length).toBe(2);
    });

    it('GET /api/node (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/node');
      expect(res.status).toBe(401);
    });

    it('GET /api/node/:id (Success) -> 200 OK', async () => {
      mockNodeService.getNodeStatus.mockResolvedValue({ nodeId: 1, health: 'OK' });

      const res = await request(app.getHttpServer())
        .get('/api/node/1')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.nodeId).toBe(1);
    });

    it('GET /api/node/:id (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/node/1');
      expect(res.status).toBe(401);
    });

    it('GET /api/node/:id (Invalid Param) -> 400 Bad Request (non-numeric id)', async () => {
      const res = await request(app.getHttpServer())
        .get('/api/node/abc')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });

    it('PUT /api/node/:id/calibration (Success) -> 200 OK', async () => {
      mockNodeService.updateCalibration.mockResolvedValue({
        nodeId: 1,
        calibrationStatus: 'CALIBRATED',
      });

      const res = await request(app.getHttpServer())
        .put('/api/node/1/calibration')
        .set('Authorization', `Bearer ${validToken}`)
        .send({
          pulses_per_litre: 450.5,
          reference_volume_ml: 1000,
          sensor_serial: 'FS-YF201-001',
        });
      expect(res.status).toBe(200);
      expect(res.body.calibrationStatus).toBe('CALIBRATED');
    });

    it('PUT /api/node/:id/calibration (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .put('/api/node/1/calibration')
        .send({ pulses_per_litre: 450 });
      expect(res.status).toBe(401);
    });

    it('PUT /api/node/:id/calibration (Invalid Input) -> 400 Bad Request (pulses_per_litre <= 0)', async () => {
      const res = await request(app.getHttpServer())
        .put('/api/node/1/calibration')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ pulses_per_litre: -5 });
      expect(res.status).toBe(400);
    });

    it('POST /api/node/:id/fault-reset (Success) -> 200 OK', async () => {
      mockNodeService.resetFault.mockResolvedValue({ nodeId: 1, health: 'OK' });

      const res = await request(app.getHttpServer())
        .post('/api/node/1/fault-reset')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.health).toBe('OK');
    });

    it('POST /api/node/:id/fault-reset (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).post('/api/node/1/fault-reset');
      expect(res.status).toBe(401);
    });

    it('POST /api/node/:id/fault-reset (Invalid Param) -> 400 Bad Request (non-numeric id)', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/node/non-number/fault-reset')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });
  });

  // =========================================================================
  // 7. PUMP COMMAND ENDPOINTS
  // =========================================================================
  describe('7. PumpCommand Module Endpoints', () => {
    it('POST /api/group/:groupId/command (Success) -> 201 Created', async () => {
      mockGroupService.getGroupStatus.mockResolvedValue({
        groupId: 1,
        status: 'ACTIVE',
        nodes: [{ node_id: 1 }],
        treatment: { treatment_version_id: 1 },
      });
      mockPumpCommandService.sendCommand.mockResolvedValue({
        command_id: 'cmd-uuid-1234',
        outcome: 'PENDING',
      });

      const res = await request(app.getHttpServer())
        .post('/api/group/1/command')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ action: 'ON', run_lease_ms: 10000, node_id: 1 });
      expect(res.status).toBe(201);
    });

    it('POST /api/group/:groupId/command (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/group/1/command')
        .send({ action: 'ON', run_lease_ms: 10000 });
      expect(res.status).toBe(401);
    });

    it('POST /api/group/:groupId/command (Conflict) -> 409 Conflict when group UNASSIGNED', async () => {
      mockGroupService.getGroupStatus.mockResolvedValue({
        groupId: 1,
        status: 'UNASSIGNED',
      });

      const res = await request(app.getHttpServer())
        .post('/api/group/1/command')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ action: 'ON', run_lease_ms: 10000 });
      expect(res.status).toBe(409);
    });

    it('POST /api/group/:groupId/command (Invalid Input) -> 400 Bad Request (invalid action / run_lease_ms)', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/group/1/command')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ action: 'INVALID_ACTION', run_lease_ms: 10 });
      expect(res.status).toBe(400);
    });

    it('GET /api/node/:nodeId/commands (Success) -> 200 OK', async () => {
      mockPumpCommandService.getNodeCommands.mockResolvedValue([
        { command_id: 'cmd-1', outcome: 'FLOW_CONFIRMED' },
      ]);

      const res = await request(app.getHttpServer())
        .get('/api/node/1/commands?limit=10&offset=0')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(Array.isArray(res.body)).toBe(true);
    });

    it('GET /api/node/:nodeId/commands (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/node/1/commands');
      expect(res.status).toBe(401);
    });

    it('GET /api/node/:nodeId/commands (Invalid Query) -> 400 Bad Request (limit > 200)', async () => {
      const res = await request(app.getHttpServer())
        .get('/api/node/1/commands?limit=500')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });
  });

  // =========================================================================
  // 8. FLOW MODULE ENDPOINTS
  // =========================================================================
  describe('8. Flow Module Endpoints', () => {
    it('GET /api/node/:id/flow (Success) -> 200 OK', async () => {
      mockFlowService.getHistory.mockResolvedValue({
        node_id: 1,
        total_delivered_volume_ml: 5000,
        flow_confirmation_rate_pct: 100,
        events: [],
      });

      const res = await request(app.getHttpServer())
        .get('/api/node/1/flow?hours=24&limit=50')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.node_id).toBe(1);
    });

    it('GET /api/node/:id/flow (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/node/1/flow');
      expect(res.status).toBe(401);
    });

    it('GET /api/node/:id/flow (Invalid Query) -> 400 Bad Request (hours > 720)', async () => {
      const res = await request(app.getHttpServer())
        .get('/api/node/1/flow?hours=721')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });

    it('GET /api/node/:id/calibration (Success) -> 200 OK', async () => {
      mockFlowService.getCalibration.mockResolvedValue({
        active: { pulses_per_litre: 450 },
        history: [],
      });

      const res = await request(app.getHttpServer())
        .get('/api/node/1/calibration')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.active.pulses_per_litre).toBe(450);
    });

    it('GET /api/node/:id/calibration (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/node/1/calibration');
      expect(res.status).toBe(401);
    });
  });

  // =========================================================================
  // 9. TUYA MEASUREMENT ENDPOINTS
  // =========================================================================
  describe('9. Tuya Bridge Module Endpoints', () => {
    it('POST /api/measurement/trigger (Success) -> 201 Created', async () => {
      mockTuyaBridgeService.measureOnDemand.mockResolvedValue({
        session_id: 'session-uuid',
        ph_value: 6.2,
        ec_value: 1.4,
      });

      const res = await request(app.getHttpServer())
        .post('/api/measurement/trigger')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ trigger_type: 'ON_DEMAND' });
      expect(res.status).toBe(201);
      expect(res.body.ph_value).toBe(6.2);
    });

    it('POST /api/measurement/trigger (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/measurement/trigger')
        .send({ trigger_type: 'ON_DEMAND' });
      expect(res.status).toBe(401);
    });

    it('POST /api/measurement/trigger (Rate Limit / Cooldown) -> 429 Too Many Requests', async () => {
      mockTuyaBridgeService.measureOnDemand.mockRejectedValue(
        new HttpException('Device cooling down', HttpStatus.TOO_MANY_REQUESTS),
      );

      const res = await request(app.getHttpServer())
        .post('/api/measurement/trigger')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ trigger_type: 'ON_DEMAND' });
      expect(res.status).toBe(429);
    });

    it('POST /api/measurement/trigger (Invalid Input) -> 400 Bad Request (SCHEDULED trigger rejected)', async () => {
      const res = await request(app.getHttpServer())
        .post('/api/measurement/trigger')
        .set('Authorization', `Bearer ${validToken}`)
        .send({ trigger_type: 'SCHEDULED' });
      expect(res.status).toBe(400);
    });

    it('GET /api/measurement/latest (Success) -> 200 OK', async () => {
      mockTuyaBridgeService.getLatest.mockResolvedValue({
        ph_value: 6.5,
        ec_value: 1.5,
      });

      const res = await request(app.getHttpServer())
        .get('/api/measurement/latest')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.ph_value).toBe(6.5);
    });

    it('GET /api/measurement/latest (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/measurement/latest');
      expect(res.status).toBe(401);
    });

    it('GET /api/measurement/history (Success) -> 200 OK', async () => {
      mockTuyaBridgeService.getHistory.mockResolvedValue({
        total: 2,
        limit: 10,
        offset: 0,
        data: [{ ph_value: 6.1 }, { ph_value: 6.2 }],
      });

      const res = await request(app.getHttpServer())
        .get('/api/measurement/history?limit=10&offset=0')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.data.length).toBe(2);
    });

    it('GET /api/measurement/history (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/measurement/history');
      expect(res.status).toBe(401);
    });

    it('GET /api/measurement/history (Invalid Query) -> 400 Bad Request (SCHEDULED rejected)', async () => {
      const res = await request(app.getHttpServer())
        .get('/api/measurement/history?trigger_type=SCHEDULED')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(400);
    });
  });

  // =========================================================================
  // 10. DEVICE STATUS ENDPOINT
  // =========================================================================
  describe('10. Device Module Endpoints', () => {
    it('GET /api/device/:id/status (Success) -> 200 OK', async () => {
      mockDeviceService.getDeviceStatus.mockResolvedValue({
        device_id: 'esp32_gw_01',
        status: 'online',
        uptime_s: 7200,
        rssi_dbm: -58,
        free_heap_b: 195000,
        ntp_synced: true,
        rtc_valid: true,
        last_seen_at: new Date(),
      });

      const res = await request(app.getHttpServer())
        .get('/api/device/esp32_gw_01/status')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(200);
      expect(res.body.device_id).toBe('esp32_gw_01');
      expect(res.body.status).toBe('online');
    });

    it('GET /api/device/:id/status (Auth Fail) -> 401 Unauthorized', async () => {
      const res = await request(app.getHttpServer()).get('/api/device/esp32_gw_01/status');
      expect(res.status).toBe(401);
    });

    it('GET /api/device/:id/status (Not Found) -> 404 Not Found', async () => {
      mockDeviceService.getDeviceStatus.mockRejectedValue(
        new NotFoundException('Device status not found for device: unknown_gw'),
      );

      const res = await request(app.getHttpServer())
        .get('/api/device/unknown_gw/status')
        .set('Authorization', `Bearer ${validToken}`);
      expect(res.status).toBe(404);
    });
  });
});

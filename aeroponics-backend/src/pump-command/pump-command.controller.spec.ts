import { Test, TestingModule } from '@nestjs/testing';
import { ConflictException, BadRequestException } from '@nestjs/common';
import { GUARDS_METADATA } from '@nestjs/common/constants';

import { PumpCommandController } from './pump-command.controller';
import { PumpCommandService } from './pump-command.service';
import { GroupService } from '../group/group.service';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { PumpAction, CommandSource, PumpCommandOutcome } from './entities/pump_command.entity';

describe('PumpCommandController (S3-F4)', () => {
  let controller: PumpCommandController;
  let pumpCommandService: any;
  let groupService: any;

  beforeEach(async () => {
    pumpCommandService = {
      sendCommand: jest.fn().mockImplementation((nodeId, groupId, action, tvId, opts) => ({
        command_id: `cmd-${nodeId}`,
        node_id: nodeId,
        group_id: groupId,
        action,
        treatment_version_id: tvId,
        run_lease_ms: opts?.runLeaseMs ?? 30000,
        outcome: PumpCommandOutcome.PENDING,
      })),
      getNodeCommands: jest.fn().mockResolvedValue([]),
    };

    groupService = {
      getGroupStatus: jest.fn(),
    };

    const module: TestingModule = await Test.createTestingModule({
      controllers: [PumpCommandController],
      providers: [
        {
          provide: PumpCommandService,
          useValue: pumpCommandService,
        },
        {
          provide: GroupService,
          useValue: groupService,
        },
      ],
    }).compile();

    controller = module.get<PumpCommandController>(PumpCommandController);
  });

  describe('JWT Guard Enforcement', () => {
    it('should be protected by JwtAuthGuard at controller level', () => {
      const guards = Reflect.getMetadata(GUARDS_METADATA, PumpCommandController);
      expect(guards).toBeDefined();
      expect(guards).toContain(JwtAuthGuard);
    });
  });

  describe('POST /api/group/:groupId/command', () => {
    it('should throw ConflictException (409) if group status is UNASSIGNED', async () => {
      groupService.getGroupStatus.mockResolvedValueOnce({
        groupId: 1,
        status: 'UNASSIGNED',
        nodes: [],
        treatment: null,
      });

      await expect(
        controller.sendGroupCommand(1, { action: PumpAction.ON }),
      ).rejects.toThrow(ConflictException);

      expect(pumpCommandService.sendCommand).not.toHaveBeenCalled();
    });

    it('should throw ConflictException (409) if group status is not ACTIVE (e.g. DISABLED)', async () => {
      groupService.getGroupStatus.mockResolvedValueOnce({
        groupId: 1,
        status: 'DISABLED',
        nodes: [],
        treatment: null,
      });

      await expect(
        controller.sendGroupCommand(1, { action: PumpAction.ON }),
      ).rejects.toThrow(ConflictException);
    });

    it('should throw ConflictException (409) if group is ACTIVE but has no active nodes', async () => {
      groupService.getGroupStatus.mockResolvedValueOnce({
        group_id: 1,
        status: 'ACTIVE',
        nodes: [],
        treatment: { treatment_version_id: 10 },
      });

      await expect(
        controller.sendGroupCommand(1, { action: PumpAction.ON }),
      ).rejects.toThrow(ConflictException);
    });

    it('should throw BadRequestException if targeted node_id is not in group active nodes', async () => {
      groupService.getGroupStatus.mockResolvedValueOnce({
        group_id: 1,
        status: 'ACTIVE',
        nodes: [{ node_id: 4 }, { node_id: 5 }],
        treatment: { treatment_version_id: 10 },
      });

      await expect(
        controller.sendGroupCommand(1, { action: PumpAction.ON, node_id: 6 }),
      ).rejects.toThrow(BadRequestException);
    });

    it('should send command to targeted node when node_id is specified in DTO', async () => {
      groupService.getGroupStatus.mockResolvedValueOnce({
        group_id: 1,
        status: 'ACTIVE',
        nodes: [{ node_id: 4 }, { node_id: 5 }],
        treatment: { treatment_version_id: 10 },
      });

      const result = await controller.sendGroupCommand(1, {
        action: PumpAction.ON,
        node_id: 5,
        run_lease_ms: 25000,
        source: CommandSource.MANUAL_OVERRIDE,
      });

      expect(result).toHaveLength(1);
      expect(result[0].node_id).toBe(5);
      expect(pumpCommandService.sendCommand).toHaveBeenCalledTimes(1);
      expect(pumpCommandService.sendCommand).toHaveBeenCalledWith(
        5,
        1,
        PumpAction.ON,
        10,
        expect.objectContaining({ runLeaseMs: 25000 }),
      );
    });

    it('should fan-out commands to all active nodes in group when node_id is omitted', async () => {
      groupService.getGroupStatus.mockResolvedValueOnce({
        group_id: 2,
        status: 'ACTIVE',
        nodes: [{ node_id: 4 }, { node_id: 5 }, { node_id: 6 }],
        treatment: { treatment_version_id: 15 },
      });

      const result = await controller.sendGroupCommand(2, {
        action: PumpAction.OFF,
        override_duration_ms: 120000,
      });

      expect(result).toHaveLength(3);
      expect(pumpCommandService.sendCommand).toHaveBeenCalledTimes(3);
      expect(pumpCommandService.sendCommand).toHaveBeenNthCalledWith(
        1,
        4,
        2,
        PumpAction.OFF,
        15,
        expect.objectContaining({ overrideDurationMs: 120000 }),
      );
      expect(pumpCommandService.sendCommand).toHaveBeenNthCalledWith(
        2,
        5,
        2,
        PumpAction.OFF,
        15,
        expect.objectContaining({ overrideDurationMs: 120000 }),
      );
      expect(pumpCommandService.sendCommand).toHaveBeenNthCalledWith(
        3,
        6,
        2,
        PumpAction.OFF,
        15,
        expect.objectContaining({ overrideDurationMs: 120000 }),
      );
    });
  });

  describe('POST /api/node/:nodeId/override', () => {
    it('should send pump override directly to node with lease', async () => {
      const result = await controller.sendNodeOverride(5, {
        action: PumpAction.ON,
        run_lease_ms: 20000,
        source: CommandSource.MANUAL_OVERRIDE,
      });

      expect(result).toBeDefined();
      expect(pumpCommandService.sendCommand).toHaveBeenCalledWith(
        5,
        null,
        PumpAction.ON,
        null,
        expect.objectContaining({
          runLeaseMs: 20000,
          source: CommandSource.MANUAL_OVERRIDE,
        }),
      );
    });
  });

  describe('GET /api/node/:nodeId/commands', () => {
    it('should delegate to getNodeCommands with limit and offset', async () => {
      const mockCommands = [
        { command_id: 'cmd-1', node_id: 4 },
        { command_id: 'cmd-2', node_id: 4 },
      ];
      pumpCommandService.getNodeCommands.mockResolvedValueOnce(mockCommands);

      const result = await controller.getNodeCommands(4, {
        limit: 50,
        offset: 0,
      });

      expect(result).toBe(mockCommands);
      expect(pumpCommandService.getNodeCommands).toHaveBeenCalledWith(4, 50, 0);
    });
  });
});

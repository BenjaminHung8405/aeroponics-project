import 'reflect-metadata';
import { Test, TestingModule } from '@nestjs/testing';
import { AuthService } from './auth.service';
import { JwtService } from '@nestjs/jwt';
import { ConfigService } from '@nestjs/config';
import { UnauthorizedException } from '@nestjs/common';

describe('AuthService', () => {
  let service: AuthService;
  let jwtService: JwtService;

  const mockConfigService = {
    get: jest.fn((key: string, defaultValue?: any) => {
      if (key === 'ADMIN_USERNAME') return 'admin';
      if (key === 'ADMIN_PASSWORD') return 'Aeroponics2026!';
      if (key === 'JWT_SECRET') return 'secret';
      return defaultValue;
    }),
  };

  beforeEach(async () => {
    const module: TestingModule = await Test.createTestingModule({
      providers: [
        AuthService,
        {
          provide: JwtService,
          useValue: {
            sign: jest.fn(() => 'mock_jwt_token_xyz'),
          },
        },
        {
          provide: ConfigService,
          useValue: mockConfigService,
        },
      ],
    }).compile();

    service = module.get<AuthService>(AuthService);
    jwtService = module.get<JwtService>(JwtService);
  });

  it('should validate admin user with correct credentials', async () => {
    const result = await service.validateUser('admin', 'Aeroponics2026!');
    expect(result).toEqual({ username: 'admin', role: 'admin' });
  });

  it('should return null for invalid credentials', async () => {
    const result = await service.validateUser('admin', 'wrong_pass');
    expect(result).toBeNull();
  });

  it('should return access_token when authenticate succeeds', async () => {
    const result = await service.authenticate('admin', 'Aeroponics2026!');
    expect(result).toEqual({ access_token: 'mock_jwt_token_xyz' });
    expect(jwtService.sign).toHaveBeenCalled();
  });

  it('should throw UnauthorizedException when authenticate fails', async () => {
    await expect(service.authenticate('admin', 'wrong_pass')).rejects.toThrow(
      UnauthorizedException,
    );
  });
});

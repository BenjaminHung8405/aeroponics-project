import { Injectable } from '@nestjs/common';
import { PassportStrategy } from '@nestjs/passport';
import { ExtractJwt, Strategy } from 'passport-jwt';
import { ConfigService } from '@nestjs/config';
import type { Request } from 'express';

export interface JwtPayload {
  sub: string;
  username: string;
  role: string;
  device_id?: string;
  iat?: number;
  exp?: number;
}

@Injectable()
export class JwtStrategy extends PassportStrategy(Strategy) {
  constructor(configService: ConfigService) {
    super({
      jwtFromRequest: ExtractJwt.fromExtractors([
        ExtractJwt.fromAuthHeaderAsBearerToken(),
        (req: Request) => {
          if (req && (req as unknown as { cookies?: Record<string, string> }).cookies?.access_token) {
            return (req as unknown as { cookies: Record<string, string> }).cookies.access_token;
          }
          if (req?.headers?.cookie) {
            const match = req.headers.cookie.match(/(?:^|;\s*)access_token=([^;]+)/);
            return match ? decodeURIComponent(match[1]) : null;
          }
          return null;
        },
      ]),
      ignoreExpiration: false,
      secretOrKey: configService.get<string>(
        'JWT_SECRET',
        'aeroponics_super_secret_jwt_key_default_32chars',
      ),
    });
  }

  async validate(payload: JwtPayload) {
    return {
      userId: payload.sub,
      username: payload.username,
      role: payload.role,
      device_id: payload.device_id,
    };
  }
}

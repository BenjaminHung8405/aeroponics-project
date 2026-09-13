import { Injectable, UnauthorizedException } from '@nestjs/common';
import { JwtService } from '@nestjs/jwt';
import { ConfigService } from '@nestjs/config';
import * as crypto from 'crypto';

@Injectable()
export class AuthService {
  constructor(
    private readonly jwtService: JwtService,
    private readonly configService: ConfigService,
  ) {}

  async validateUser(username: string, pass: string): Promise<any> {
    const adminUser = this.configService.get<string>('ADMIN_USERNAME', 'admin');
    const adminPass = this.configService.get<string>('ADMIN_PASSWORD', 'Aeroponics2026!');

    const userBuffer = Buffer.from(username);
    const expectedUserBuffer = Buffer.from(adminUser);
    const passBuffer = Buffer.from(pass);
    const expectedPassBuffer = Buffer.from(adminPass);

    const isUserValid =
      userBuffer.length === expectedUserBuffer.length &&
      crypto.timingSafeEqual(userBuffer, expectedUserBuffer);

    const isPassValid =
      passBuffer.length === expectedPassBuffer.length &&
      crypto.timingSafeEqual(passBuffer, expectedPassBuffer);

    if (isUserValid && isPassValid) {
      return { username, role: 'admin' };
    }

    return null;
  }

  async login(user: { username: string; role?: string }) {
    const payload = { username: user.username, sub: user.username, role: user.role || 'admin' };
    return {
      access_token: this.jwtService.sign(payload),
    };
  }

  async authenticate(username: string, pass: string) {
    const user = await this.validateUser(username, pass);
    if (!user) {
      throw new UnauthorizedException('Invalid credentials');
    }
    return this.login(user);
  }
}

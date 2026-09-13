export interface ITuyaDevice {
  connect(): Promise<boolean>;
  disconnect(): void;
  isConnected(): boolean;
  get(options?: { schema?: boolean; dps?: number }): Promise<any>;
}

export interface TuyaDeviceOptions {
  id: string;
  key: string;
  ip?: string;
  port?: number;
  version?: number | string;
  issueGetOnConnect?: boolean;
  issueRefreshOnConnect?: boolean;
}

export type TuyaDeviceFactory = (options: TuyaDeviceOptions) => ITuyaDevice;

export const TUYA_CLIENT_FACTORY = 'TUYA_CLIENT_FACTORY';

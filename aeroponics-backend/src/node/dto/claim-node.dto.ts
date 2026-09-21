import { IsInt, IsIn } from 'class-validator';
import { AGU_LEGACY_NODE_IDS } from '../node-topology';

export class ClaimNodeDto {
  @IsInt()
  @IsIn(AGU_LEGACY_NODE_IDS)
  fromNodeId!: number;

  @IsInt()
  @IsIn(AGU_LEGACY_NODE_IDS)
  toNodeId!: number;
}

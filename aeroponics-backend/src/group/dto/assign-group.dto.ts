import {
  IsInt,
  Min,
  Max,
  IsArray,
  ArrayMinSize,
  ArrayMaxSize,
  ArrayUnique,
  IsIn,
} from 'class-validator';
import { Type } from 'class-transformer';
import { AGU_LEGACY_NODE_IDS } from '../../node/node-topology';

export class AssignGroupDto {
  @IsInt({ message: 'treatment_version_id must be an integer' })
  @Min(1, { message: 'treatment_version_id must be at least 1' })
  @Type(() => Number)
  treatment_version_id: number;

  @IsArray({ message: 'node_ids must be an array of integers' })
  @ArrayMinSize(1, { message: 'node_ids must contain at least 1 node ID' })
  @ArrayMaxSize(4, { message: 'node_ids cannot contain more than 4 node IDs' })
  @ArrayUnique({ message: 'node_ids cannot contain duplicate entries' })
  @IsInt({ each: true, message: 'Each node ID in node_ids must be an integer' })
  @IsIn(AGU_LEGACY_NODE_IDS, { each: true, message: 'Node ID must be one of 4, 5, 6, 7' })
  @Type(() => Number)
  node_ids: number[];
}

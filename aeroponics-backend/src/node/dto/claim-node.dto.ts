import { IsInt, Min, Max } from 'class-validator';

export class ClaimNodeDto {
  @IsInt()
  @Min(1)
  @Max(254)
  fromNodeId!: number;

  @IsInt()
  @Min(1)
  @Max(4)
  toNodeId!: number;
}

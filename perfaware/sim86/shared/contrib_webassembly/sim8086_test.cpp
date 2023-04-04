#include "sim8086.h"
#include "custom_fprintf.cpp"
#include "../../sim86_lib.cpp"

register_bank RegisterBank = {};

static output_memory 
AllocateOutputBuffer(segmented_access MainMemory, u64 MaxMemory)
{
  output_memory Mem = {};
  Mem.Base = MainMemory.Memory + (1 << 20);
  Mem.Max = MaxMemory - (1 << 20);
  Mem.Used = 0;
  return Mem;
}

static segmented_access 
AllocateMemoryPow2(u8 *Memory, u32 SizePow2)
{
  segmented_access Result = FixedMemoryPow2(SizePow2, Memory);
  return Result;
}

static void 
DisAsm8086Wasm(u32 DisAsmByteCount, segmented_access DisAsmStart)
{
  segmented_access At = DisAsmStart;

  instruction_table Table = Get8086InstructionTable();

  u32 Count = DisAsmByteCount;
  while(Count)
  {
    instruction Instruction = DecodeInstruction(Table, At);
    if(Instruction.Op)
    {
      if(Count >= Instruction.Size)
      {
        At = MoveBaseBy(At, Instruction.Size);
        Count -= Instruction.Size;
      }
      else
      {
        fprintf(stderr, "ERROR: Instruction extends outside disassembly region\n\0");
        break;
      }
      
      PrintInstruction(Instruction, stdout);
      fprintf(stdout, "\n\0");
    }
    else
    {
      fprintf(stderr, "ERROR: Unrecognized binary in instruction stream.\n\0");
      break;
    }
  }
}

extern "C" u8 * 
Entry(u8 *Memory, u32 BytesRead, u64 MaxMemory)
{
  segmented_access MainMemory = AllocateMemoryPow2(Memory, 20);
  OutputMemory = AllocateOutputBuffer(MainMemory, MaxMemory);

  // Reserve space to store how many bytes to read on javascript side
  u32 *OutputSize = (u32 *)OutputMemory.Base;
  OutputMemory.Used = 4;

  // Do the disassembly
  DisAsm8086Wasm(BytesRead, MainMemory);

  // Add the number of bytes to read excluding the number itself
  *OutputSize = OutputMemory.Used;
  return OutputMemory.Base;
}

void
SetRegister(register_access Register, u16 Value)
{
  u8 NumRegisters = ArrayCount(RegisterBank.Registers);
  u8 RegisterIndex = Register.Index % NumRegisters;

  if (Register.Count == 2) 
  {
    // Full
    RegisterBank.Registers[RegisterIndex] = Value;
  }
  else if ((Register.Offset & 1) == 0)
  {
    // Low
    u8 CurrentHigh = RegisterBank.Registers[RegisterIndex] >> 8;
    RegisterBank.Registers[RegisterIndex] = (CurrentHigh << 8) | Value;
  }
  else 
  {
    // High
    u8 CurrentLow = RegisterBank.Registers[RegisterIndex] & 0xff;
    RegisterBank.Registers[RegisterIndex] = (Value << 8) | CurrentLow;
  }
}

u16
GetRegister(register_access Register)
{
  u16 Result = 0;
  u8 NumRegisters = ArrayCount(RegisterBank.Registers);
  u8 RegisterIndex = Register.Index % NumRegisters;

  if (Register.Count == 2) 
  {
    // Full
    Result = RegisterBank.Registers[RegisterIndex];
  }
  else if ((Register.Offset & 1) == 0)
  {
    // Low
    Result = RegisterBank.Registers[RegisterIndex] & 0xff;
  }
  else 
  {
    // High
    Result = RegisterBank.Registers[RegisterIndex] >> 8;
  }

  return Result;
}

flags 
RegisterToFlags(u16 FlagsRegister)
{
  flags Result = {};
  Result.OF = (0x0800 & FlagsRegister) >> 11;
  Result.DF = (0x0400 & FlagsRegister) >> 10;
  Result.IF = (0x0200 & FlagsRegister) >> 9;
  Result.TF = (0x0100 & FlagsRegister) >> 8;
  Result.SF = (0x0080 & FlagsRegister) >> 7;
  Result.ZF = (0x0040 & FlagsRegister) >> 6;
  Result.AF = (0x0010 & FlagsRegister) >> 4;
  Result.PF = (0x0002 & FlagsRegister) >> 2;
  Result.CF = (0x0001 & FlagsRegister);
  return Result;
}

u16
FlagsToRegister(flags Flags)
{
  u16 Result = (Flags.OF << 11) || 
    (Flags.DF << 10) ||
    (Flags.IF << 9) ||
    (Flags.TF << 8) ||
    (Flags.SF << 7) ||
    (Flags.ZF << 6) ||
    (Flags.AF << 4) ||
    (Flags.PF << 2) ||
    (Flags.CF);

  return Result;
}

inline void
SetFlags(s32 Result)
{
  u16 FlagsRegister = RegisterBank.Registers[14];
  flags Flags = RegisterToFlags(FlagsRegister);
  // Flags to update AF, CF, OF, PF, SF, ZF
  Flags.OF = (Result > 65535) ? 1 : 0;
  Flags.SF = (((Result & 0x8000) >> 15) == 1) ? 1 : 0;
  Flags.ZF = (Result == 0) ? 1 : 0;
  Flags.CF = ((Result > 65535) || (Result < -65536)) ? 1 : 0;
  u16 FinalRegister = FlagsToRegister(Flags);
  RegisterBank.Registers[14] = FinalRegister;
}

extern "C" register_bank *
Step(u8 *Memory, u32 BytesRead, u64 MaxMemory)
{
  segmented_access MainMemory = AllocateMemoryPow2(Memory, 20); 
  OutputMemory = AllocateOutputBuffer(MainMemory, MaxMemory);

  instruction_table Table = Get8086InstructionTable();
  
  MainMemory = MoveBaseBy(MainMemory, RegisterBank.CurrentByte);

  instruction Instruction = DecodeInstruction(Table, MainMemory);
  RegisterBank.CurrentByte += Instruction.Size;
  RegisterBank.CurrentInstruction += 1;
  if (RegisterBank.CurrentByte >= BytesRead) 
  {
    RegisterBank.CurrentByte = 0;
    RegisterBank.CurrentInstruction = 0;
  }
  
  if (Instruction.Op == Op_mov)
  {
    if (Instruction.Operands[1].Type == Operand_Immediate)
    {
      u16 ImmediateValue = (u16)(Instruction.Operands[1].Immediate.Value);
      SetRegister(Instruction.Operands[0].Register, ImmediateValue);
    } 
    else 
    {
      u16 Value = GetRegister(Instruction.Operands[1].Register);
      SetRegister(Instruction.Operands[0].Register, Value);
    }
  }
  else if (Instruction.Op == Op_add)
  {
    s32 SourceValue;
    if (Instruction.Operands[1].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[1].Immediate.Value;
    }
    else if (Instruction.Operands[1].Type == Operand_Register)
    {
      SourceValue = GetRegister(Instruction.Operands[1].Register);
    }

    s32 DestValue = GetRegister(Instruction.Operands[0].Register);
    s32 Result = DestValue + SourceValue;
    SetRegister(Instruction.Operands[0].Register, Result);

    SetFlags(Result);
  }
  else if (Instruction.Op == Op_cmp)
  {
    s32 SourceValue;
    if (Instruction.Operands[1].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[1].Immediate.Value;
    }
    else if (Instruction.Operands[1].Type == Operand_Register)
    {
      SourceValue = GetRegister(Instruction.Operands[1].Register);
    }

    s32 DestValue = GetRegister(Instruction.Operands[0].Register);
    s32 Result = DestValue - SourceValue;

    SetFlags(Result);
  }
  else if (Instruction.Op == Op_sub)
  {
    s32 SourceValue;
    if (Instruction.Operands[1].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[1].Immediate.Value;
    }
    else if (Instruction.Operands[1].Type == Operand_Register)
    {
      SourceValue = GetRegister(Instruction.Operands[1].Register);
    }

    s32 DestValue = GetRegister(Instruction.Operands[0].Register);
    s32 Result = DestValue - SourceValue;
    SetRegister(Instruction.Operands[0].Register, Result);

    SetFlags(Result);
  }
  else 
  {
    // No other instructions have been implemented
    assert(0);
  }

  return &RegisterBank;
}

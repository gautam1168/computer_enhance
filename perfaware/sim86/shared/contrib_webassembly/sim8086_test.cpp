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
  u16 Result = (Flags.OF << 11) | 
    (Flags.DF << 10) |
    (Flags.IF << 9) |
    (Flags.TF << 8) |
    (Flags.SF << 7) |
    (Flags.ZF << 6) |
    (Flags.AF << 4) |
    (Flags.PF << 2) |
    (Flags.CF);

  return Result;
}

inline s32
CountBits(s32 Number)
{
  s32 NumBits = 0;
  while (Number) 
  {
    NumBits += ((Number & 0x1) == 1) ? 1 : 0;
    Number = Number >> 1;
  }
  return NumBits;
}

inline bool
IsParityEven(s32 Result)
{
  u8 LowEightBits = Result & 0xff;
  s32 NumSetBits = CountBits(LowEightBits);
  bool IsEven = ((NumSetBits % 2) == 0);
  return IsEven;
}

inline bool
OverflownBitsPresent(s32 Result)
{
  u16 OverflownBits = Result >> 16;
  s32 NumSetBits = CountBits(OverflownBits);
  return NumSetBits > 0;
}

inline bitwise_add_result
BitwiseAdd(u16 A, u16 B)
{
  s16 Addand = (s16)A;
  s16 Addee = (s16)B;
  bitwise_add_result Result = {};
  s32 CarryBit = 0;
  for (s32 BitIndex = 0; BitIndex < 16; ++BitIndex)
  {
    s32 ABit = A & 0b1;
    A = A >> 1;
    s32 BBit = B & 0b1;
    B = B >> 1;
    s32 Value = ABit + BBit + CarryBit;
    if (Value == 0)
    {
      Result.Value = Result.Value | (0 << BitIndex);
      CarryBit = 0;
    }
    else if (Value == 1)
    {
      Result.Value = Result.Value | (1 << BitIndex);
      CarryBit = 0;
    }
    else if (Value == 2)
    {
      Result.Value = Result.Value | (0 << BitIndex);
      CarryBit = 1;
    }
    else if (Value == 3)
    {
      Result.Value = Result.Value | (1 << BitIndex);
      CarryBit = 1;
    }
    else
    {
      assert(!"Invalid state during addition!");
    }

    // TODO: Auxiliary flag is only captured for low byte of 16bit value
    // Implement the nibble thing
    if ((BitIndex == 7) && (CarryBit == 1))
    {
      Result.AuxiliaryCarry = true;
    }

    if (BitIndex == 15 && CarryBit == 1)
    {
      Result.Carry = true;
    }

    if ((Addand > 0 && Addee > 0 && Result.Value < 0) || 
        (Addand < 0 && Addee < 0 && Result.Value > 0))
    {
      Result.Overflow = true;
    }
  }

  return Result;
}

inline u16
GetTwosComplement(s16 Value)
{
  u16 Result = 0;
  for (s32 BitIndex = 0; BitIndex < 16; ++BitIndex)
  {
    Result = Result | (!(Value & 0b1) << BitIndex);
  }

  BitwiseAdd(Result, 1);
  return Result;
}

inline bitwise_add_result
BitwiseSub(u16 A, u16 B)
{
  s16 Minuhend = (s16)A;
  s16 Subtrahend = (s16)B;
  bitwise_add_result Result = {};

  s32 Borrow[16];
  s32 ABits[16];
  s32 BBits[16];
  for (s32 BorrowArrayIndex = 0;
      BorrowArrayIndex < 16;
      ++BorrowArrayIndex)
  {
    Borrow[BorrowArrayIndex] = 0;
    ABits[BorrowArrayIndex] = A & 0b1;
    A = A >> 1;
    BBits[BorrowArrayIndex] = B & 0b1;
    B = B >> 1;
  }

  for (s32 BitIndex = 0; BitIndex < 16; ++BitIndex)
  {
    s32 ABit = ABits[BitIndex];
    s32 BBit = BBits[BitIndex];
    
    if (ABit < BBit) 
    {
      ABit = 2;
      Borrow[BitIndex] = 2;
      s32 CurrentIndex = BitIndex + 1;
      while (ABits[CurrentIndex] != 1 && CurrentIndex < 16)
      {
        Borrow[CurrentIndex] = 1;
        ABits[CurrentIndex++] = 1;
      }

      ABits[CurrentIndex] = 0;
      s32 Value = ABit - BBit;
      Result.Value = Result.Value | (Value << BitIndex);
    }
    else 
    {
      s32 Value = ABit - BBit;
      Result.Value = Result.Value | (Value << BitIndex);
    }
  }

  // TODO: Auxiliary flag is only captured for low byte of 16bit value
  // Implement the nibble thing
  if (Borrow[7] > 0)
  {
    Result.AuxiliaryCarry = true;
  }

  if (Borrow[15] > 0)
  {
    Result.Carry = true;
  }

  if ((Minuhend < 0 && Subtrahend > 0 && Result.Value > 0) || 
      (Minuhend > 0 && Subtrahend < 0 && Result.Value < 0))
  {
    Result.Overflow = true;
  }
  

  return Result;
}

inline void
SetFlags(s32 Result)
{
  u16 FlagsRegister = RegisterBank.Registers[14];
  flags Flags = RegisterToFlags(FlagsRegister);
  // Flags to update AF, CF, OF, PF, SF, ZF
  Flags.OF = (Result > 32767) ? 1 : 0;
  Flags.SF = (((Result & 0x8000) >> 15) == 1) ? 1 : 0;
  Flags.ZF = (Result == 0) ? 1 : 0;
  Flags.CF = OverflownBitsPresent(Result >> 16) ? 1 : 0;
  Flags.PF = IsParityEven(Result) ? 1 : 0;
  u16 FinalRegister = FlagsToRegister(Flags);
  RegisterBank.Registers[14] = FinalRegister;
}

inline void
SetFlags(bitwise_add_result Result)
{
  u16 FlagsRegister = RegisterBank.Registers[14];
  flags Flags = RegisterToFlags(FlagsRegister);
  // Flags to update AF, CF, OF, PF, SF, ZF
  Flags.OF = Result.Overflow ? 1 : 0;
  Flags.SF = (((Result.Value & 0x8000) >> 15) == 1) ? 1 : 0;
  Flags.ZF = (Result.Value == 0) ? 1 : 0;
  Flags.CF = Result.Carry ? 1 : 0;
  Flags.PF = IsParityEven(Result.Value) ? 1 : 0;
  Flags.AF = Result.AuxiliaryCarry ? 1 : 0;
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
    bitwise_add_result Result = BitwiseAdd(DestValue, SourceValue);
    SetRegister(Instruction.Operands[0].Register, Result.Value);

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
    bitwise_add_result Result = BitwiseSub((u16)DestValue, (u16)SourceValue);

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
    bitwise_add_result Result = BitwiseSub((u16)DestValue, (u16)SourceValue);
    SetRegister(Instruction.Operands[0].Register, Result.Value);

    SetFlags(Result);
  }
  else 
  {
    // No other instructions have been implemented
    assert(0);
  }

  return &RegisterBank;
}

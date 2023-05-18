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

enum ea_cycles : u32
{
  Disp_Only = 6,
  Base_Or_Index_Only = 5, // BX,BP,SI,DI 
  Displacement_Plus_Base_Or_Index = 9, // BX,BP,SI,DI
  Base_Plus_Index_BPDI_BXSI = 7, // BP + DI, BX + SI
  Base_Plus_Index_BPSI_BXDI = 8, // BP + SI, BX + SI
  Disp_Base_Index_BPDIDISP_BXSIDISP = 11, // BP + DI + DISP, BX + SI + DISP
  Disp_Base_Index_BPSIDISP_BXDIDISP = 12 // BP + SI + DISP, BX + DI + DISP                                          
};

inline bool 
Cmp(const char *Source, const char *Dest)
{
  bool Result = true;
  if (*Source != 0 && *Dest != 0)
  {
    while (*Source != 0 && *Dest != 0)
    {
      Result = Result && (*Source == *Dest);
      Source += 1;
      Dest += 1;
    }
  }
  else
  {
    Result = false;
  }
  return Result;
}

inline int
GetEaCycles(instruction_operand Operand)
{
  effective_address_expression Address = Operand.Address;
  
  int Result = 0;

  int NumRegs = 0;
  const char *Reg1Name = 0;
  const char *Reg2Name = 0;
  for (u32 Index = 0; Index < ArrayCount(Address.Terms); ++Index)
  {
    effective_address_term Term = Address.Terms[Index];
    register_access Reg = Term.Register;
    if (Reg.Index)
    {
      NumRegs++;
      if (Reg1Name == 0)
      {
        Reg1Name = GetRegName(Reg);
      }
      else if (Reg2Name == 0)
      {
        Reg2Name = GetRegName(Reg);
      }
      else
      {
        assert(!"Three registers not allowed!");
      }
    }
  }

  bool HasDisp_Only = (Address.Displacement != 0) && (NumRegs == 0);
  bool HasBase_Or_Index_Only = (Address.Displacement == 0) && (NumRegs == 1); 
  bool HasDisplacement_Plus_Base_Or_Index = (Address.Displacement != 0) && 
    (NumRegs == 1);
  bool HasBase_Plus_Index_BPDI_BXSI = (Address.Displacement == 0) && 
    (
      (Cmp("bp", Reg1Name) && Cmp("di", Reg2Name)) ||
      (Cmp("bx", Reg1Name) && Cmp("si", Reg2Name))
    );
  bool HasBase_Plus_Index_BPSI_BXDI = (Address.Displacement == 0) && 
    (
      (Cmp("bp", Reg1Name) && Cmp("si", Reg2Name)) ||
      (Cmp("bx", Reg1Name) && Cmp("di", Reg2Name))
    );
  bool HasDisp_Base_Index_BPDIDISP_BXSIDISP = (Address.Displacement != 0) &&
    (
     (Cmp("bp", Reg1Name) && Cmp("di", Reg2Name)) ||
     (Cmp("bx", Reg1Name) && Cmp("si", Reg2Name))
    );
  bool HasDisp_Base_Index_BPSIDISP_BXDIDISP = (Address.Displacement != 0) &&
    (
     (Cmp("bp", Reg1Name) && Cmp("si", Reg2Name)) ||
     (Cmp("bx", Reg1Name) && Cmp("di", Reg2Name))
    );

  if (HasDisp_Only)
  {
    Result = 6;
  }
  else if (HasBase_Or_Index_Only)
  {
    Result = 5;
  }
  else if (HasDisplacement_Plus_Base_Or_Index)
  {
    Result = 9;
  }
  else if (HasBase_Plus_Index_BPDI_BXSI)
  {
    Result = 7;
  }
  else if (HasBase_Plus_Index_BPSI_BXDI)
  {
    Result = 8;
  }
  else if (HasDisp_Base_Index_BPDIDISP_BXSIDISP)
  {
    Result = 11;
  }
  else if (HasDisp_Base_Index_BPSIDISP_BXDIDISP)
  {
    Result = 12;
  }
  else
  {
    assert(!"Unrecognized addressing scheme");
  }

  return Result;
}

static void
PrintCycleCount(instruction Instruction)
{
  int Cycles = 0;
  // If instruction should have ea_cycles then add that
  int EaCycles = 0;

  int TransferCycles = 0;

  instruction_operand SourceOperand = Instruction.Operands[1];
  instruction_operand DestOperand = Instruction.Operands[0];

  // Find the Instruction in cycle_counts and get Cycles
  if (Instruction.Op == Op_mov)
  {
    if (DestOperand.Type == Operand_Register && SourceOperand.Type == Operand_Memory)
    {
      const char *RegName = GetRegName(DestOperand.Register);
      bool IsAccumulator = Cmp("al", RegName);
      if (IsAccumulator)
      {
        Cycles = 10;
        EaCycles = 0;
      }
      else
      {
        Cycles = 8;
        EaCycles = GetEaCycles(SourceOperand);
      }

      TransferCycles = 4;
    }
    else if (DestOperand.Type == Operand_Memory && SourceOperand.Type == Operand_Register)
    {
      const char *RegName = GetRegName(DestOperand.Register);
      bool IsAccumulator = Cmp("al", RegName);
      if (IsAccumulator)
      {
        Cycles = 10;
        EaCycles = 0;
      }
      else
      {
        Cycles = 9;
        EaCycles = GetEaCycles(DestOperand);
      }
      TransferCycles = 4;
    }
    else if (DestOperand.Type == Operand_Register && SourceOperand.Type == Operand_Register)
    {
      Cycles = 2;
      EaCycles = 0;
    }
    else if (DestOperand.Type == Operand_Register && SourceOperand.Type == Operand_Immediate)
    {
      Cycles = 4;
      EaCycles = 0;
    }
    else if (DestOperand.Type == Operand_Memory && SourceOperand.Type == Operand_Immediate)
    {
      Cycles = 10;
      EaCycles = GetEaCycles(DestOperand);
      TransferCycles = 4;
    }
    else
    {
      // assert(!"Not implemented operand combination!");
      Cycles = -1;
      EaCycles = -1;
      TransferCycles = -1;
    }
    
  }
  else if (Instruction.Op == Op_add)
  {
    if (DestOperand.Type == Operand_Register && SourceOperand.Type == Operand_Memory)
    {
      Cycles = 9;
      EaCycles = GetEaCycles(SourceOperand);
      TransferCycles = 4;
    }
    else if (DestOperand.Type == Operand_Memory && SourceOperand.Type == Operand_Register)
    {
      Cycles = 16;
      EaCycles = GetEaCycles(DestOperand);
      TransferCycles = 8;
    }
    else if (DestOperand.Type == Operand_Register && SourceOperand.Type == Operand_Register)
    {
      Cycles = 3;
      EaCycles = 0;
    }
    else if (DestOperand.Type == Operand_Register && SourceOperand.Type == Operand_Immediate)
    {
      Cycles = 4;
      EaCycles = 0;
    }
    else if (DestOperand.Type == Operand_Memory && SourceOperand.Type == Operand_Immediate)
    {
      Cycles = 17;
      EaCycles = GetEaCycles(DestOperand);
      TransferCycles = 8;
    }
    else
    {
      // assert(!"Not implemented operand combination!");
      Cycles = -1;
      EaCycles = -1;
      TransferCycles = -1;
    }
  }
  else
  {
    // assert(!"Cycle count not implemented!");
    Cycles = -1;
    EaCycles = -1;
    TransferCycles = -1;
  }

  fprintf(stdout, "%d; %d; %d; ", Cycles, EaCycles, TransferCycles);
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
      
      fprintf(stdout, "%d; ", Instruction.Size);
      PrintCycleCount(Instruction);
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

u32
MemoryOperandToAddressOffset(instruction_operand Operand, u32 Flags)
{
  u32 Result = 0;
  assert(Operand.Type == Operand_Memory);
  effective_address_expression Address = Operand.Address;

  if (Address.Flags & Address_ExplicitSegment)
  {
    assert(!"Not implemented explicit segment stuff!");
  }

  if (Flags & Inst_Segment)
  {
    assert(!"Not implemented instruction segment stuff!");
  }

  for (u32 Index = 0; Index < ArrayCount(Address.Terms); ++Index)
  {
    effective_address_term Term = Address.Terms[Index];
    register_access Reg = Term.Register;

    if (Reg.Index)
    {
      u32 RegisterValue = GetRegister(Reg);
      if (Term.Scale != 1)
      {
        RegisterValue *= Term.Scale;
      }
      Result += RegisterValue;
    }
  }

  Result += Address.Displacement;
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
  Result.PF = (0x0004 & FlagsRegister) >> 2;
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

    if ((BitIndex == 7 || BitIndex == 3) && (CarryBit == 1))
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
  if (Borrow[3] > 0 || Borrow[7] > 0)
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

inline void
ResetFlagsForXOR()
{
  u16 FlagsRegister = RegisterBank.Registers[14];
  flags Flags = RegisterToFlags(FlagsRegister);
  // Flags to update AF, CF, OF, PF, SF, ZF
  Flags.OF = 0;
  Flags.CF = 0;
  u16 FinalRegister = FlagsToRegister(Flags);
  RegisterBank.Registers[14] = FinalRegister;
}

inline void
SetZFlag()
{
  u16 FlagsRegister = RegisterBank.Registers[14];
  flags Flags = RegisterToFlags(FlagsRegister);
  // Flags to update AF, CF, OF, PF, SF, ZF
  Flags.ZF = 0;
  u16 FinalRegister = FlagsToRegister(Flags);
  RegisterBank.Registers[14] = FinalRegister;
}

extern "C" register_bank *
Step(u8 *Memory, u32 BytesRead, u64 MaxMemory)
{
  segmented_access CodeSegment = FixedMemoryPow2(20, Memory); 
  s32 CodeSegmentAddress = GetAbsoluteAddressOf(CodeSegment, 0);
  // 64KB = 1 << 16
  segmented_access DataSegment = MoveBaseBy(CodeSegment, (1 << 16));
  s32 DataSegmentAddress = GetAbsoluteAddressOf(DataSegment, 0);

  segmented_access StackSegment = MoveBaseBy(DataSegment, (1 << 16));
  s32 StackSegmentAddress = GetAbsoluteAddressOf(StackSegment, 0);

  OutputMemory = AllocateOutputBuffer(CodeSegment, MaxMemory);

  instruction_table Table = Get8086InstructionTable();
  
  u16 *IP = &RegisterBank.Registers[13];
  segmented_access At = MoveBaseBy(CodeSegment, *IP);

  instruction Instruction = DecodeInstruction(Table, At);
  *IP += Instruction.Size;
  RegisterBank.CurrentInstruction += 1;
  /*
  if (RegisterBank.Registers[13] >= BytesRead) 
  {
    RegisterBank.Registers[13] = 0;
    RegisterBank.CurrentInstruction = 0;
  }
  */
  
  if (Instruction.Op == Op_mov)
  {
    if (Instruction.Operands[0].Type == Operand_Register && 
        Instruction.Operands[1].Type == Operand_Immediate)
    {
      u16 ImmediateValue = (u16)(Instruction.Operands[1].Immediate.Value);
      SetRegister(Instruction.Operands[0].Register, ImmediateValue);
    } 
    else if (Instruction.Operands[0].Type == Operand_Register &&
        Instruction.Operands[1].Type == Operand_Register)
    {
      u16 Value = GetRegister(Instruction.Operands[1].Register);
      SetRegister(Instruction.Operands[0].Register, Value);
    }
    else if (Instruction.Operands[0].Type == Operand_Memory &&
        Instruction.Operands[1].Type == Operand_Immediate)
    {
      u16 LogicalAddress = MemoryOperandToAddressOffset(Instruction.Operands[0], Instruction.Flags);
      u16 Value = Instruction.Operands[1].Immediate.Value; 
      u8 *MemoryLocation = AccessMemory(DataSegment, LogicalAddress);

      u8 ValueLow = Value & 0xff;
      u8 ValueHigh = Value >> 8;
      
      *MemoryLocation++ = ValueLow;
      *MemoryLocation++ = ValueHigh;
    }
    else if (Instruction.Operands[0].Type == Operand_Register &&
        Instruction.Operands[1].Type == Operand_Memory)
    {
      u16 LogicalAddress = MemoryOperandToAddressOffset(Instruction.Operands[1], Instruction.Flags);
      u8 *MemoryLocation = AccessMemory(DataSegment, LogicalAddress);

      u8 ValueLow = *MemoryLocation++;
      u8 ValueHigh = *MemoryLocation++;
      
      u16 Value = (ValueHigh << 8) | ValueLow;
      SetRegister(Instruction.Operands[0].Register, Value);
    }
    else if (Instruction.Operands[0].Type == Operand_Memory &&
        Instruction.Operands[1].Type == Operand_Register)
    {
      u16 LogicalAddress = MemoryOperandToAddressOffset(Instruction.Operands[0], Instruction.Flags);
      u16 Value = GetRegister(Instruction.Operands[1].Register);
      u8 *MemoryLocation = AccessMemory(DataSegment, LogicalAddress);

      u8 ValueLow = Value & 0xff;
      u8 ValueHigh = Value >> 8;

      *MemoryLocation++ = ValueLow;
      *MemoryLocation++ = ValueHigh;
    }
    else 
    {
      assert(!"Move variant not implemented!");
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
  else if (Instruction.Op == Op_jne)
  {
    s32 SourceValue;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[0].Immediate.Value;
    }
    else
    {
      assert(!"Cannot jump to non immediate address");
    }
    
    flags Flags = RegisterToFlags(RegisterBank.Registers[14]);
    if (Flags.ZF == 0)
    {
      *IP += SourceValue;
    }
  }
  else if (Instruction.Op == Op_je)
  {
    s32 SourceValue;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[0].Immediate.Value;
    }
    else
    {
      assert(!"Cannot jump to non immediate address");
    }
    
    flags Flags = RegisterToFlags(RegisterBank.Registers[14]);
    if (Flags.ZF == 1)
    {
      *IP += SourceValue;
    }
  }
  else if (Instruction.Op == Op_jp)
  {
    s32 SourceValue;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[0].Immediate.Value;
    }
    else
    {
      assert(!"Cannot jump to non immediate address");
    }
    
    flags Flags = RegisterToFlags(RegisterBank.Registers[14]);
    if (Flags.PF == 1)
    {
      *IP += SourceValue;
    }
  }
  else if (Instruction.Op == Op_jb)
  {
    s32 SourceValue;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[0].Immediate.Value;
    }
    else
    {
      assert(!"Cannot jump to non immediate address");
    }
    
    flags Flags = RegisterToFlags(RegisterBank.Registers[14]);
    if (Flags.CF == 1)
    {
      *IP += SourceValue;
    }
  }
  else if (Instruction.Op == Op_jmp)
  {
    s32 SourceValue;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[0].Immediate.Value;
    }
    else if (Instruction.Operands[0].Type == Operand_Register)
    {
      SourceValue = GetRegister(Instruction.Operands[1].Register);
    }
    else
    {
      assert(!"Don't know how to jump!");
    }

    *IP += SourceValue;
  }
  else if (Instruction.Op == Op_loopnz)
  {
    s32 SourceValue;
    // CX register is 3
    RegisterBank.Registers[3] -= 1;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[0].Immediate.Value;
    }
    else
    {
      assert(!"Cannot jump to non immediate address");
    }
    
    flags Flags = RegisterToFlags(RegisterBank.Registers[14]);
    if (Flags.ZF == 0 && RegisterBank.Registers[3] != 0)
    {
      *IP += SourceValue;
    }
  }
  else if (Instruction.Op == Op_loop)
  {
    s32 SourceValue;
    // CX register is 3
    RegisterBank.Registers[3] -= 1;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      SourceValue = Instruction.Operands[0].Immediate.Value;
    }
    else
    {
      assert(!"Cannot jump to non immediate address");
    }
    
    flags Flags = RegisterToFlags(RegisterBank.Registers[14]);
    if (RegisterBank.Registers[3] != 0)
    {
      *IP += SourceValue;
    }
  }
  else if (Instruction.Op == Op_pop)
  {
    if (Instruction.Operands[0].Type == Operand_Register)
    {
      u16 *SP = &RegisterBank.Registers[5]; 
      u8 *MemoryLocation = AccessMemory(StackSegment, *SP);
      u8 LowByte = *MemoryLocation++;
      u8 HighByte = *MemoryLocation++;
      *SP = *SP - 2;
      SetRegister(Instruction.Operands[0].Register, (HighByte << 8) | LowByte);
    }
    else
    {
      assert(!"Cannot pop to non register");
    }
  }
  else if (Instruction.Op == Op_push)
  {
    if (Instruction.Operands[0].Type == Operand_Register)
    {
      u16 *SP = &RegisterBank.Registers[5]; 
      u8 *MemoryLocation = AccessMemory(StackSegment, *SP);
      u16 Value = GetRegister(Instruction.Operands[0].Register);
      *MemoryLocation++ = Value & 0xff; 
      *MemoryLocation++ = Value >> 8;
      *SP = *SP + 2;
    }
    else
    {
      assert(!"Cannot push from non register");
    }
  }
  else if (Instruction.Op == Op_call)
  {
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      u16 *IP = &RegisterBank.Registers[13];
      u16 *SP = &RegisterBank.Registers[5]; 

      u8 *MemoryLocation = AccessMemory(StackSegment, *SP);
      for (s32 RegIndex = 1; RegIndex < 15; ++RegIndex)
      {
        *MemoryLocation++ = RegisterBank.Registers[RegIndex] & 0xff;
        *MemoryLocation++ = RegisterBank.Registers[RegIndex] >> 8;
        *SP += 2;
      }
      
      *IP += Instruction.Operands[0].Immediate.Value;
    }
    else
    {
      assert(!"Invalid call");
    }
  }
  else if (Instruction.Op == Op_ret)
  {
    u16 *IP = &RegisterBank.Registers[13];
    u16 *SP = &RegisterBank.Registers[5]; 

    u8 *MemoryLocation = AccessMemory(StackSegment, *SP);
    
    u16 StacksIPValue = *MemoryLocation; 
    for (s32 RegIndex = 0; RegIndex < 28; ++RegIndex)
    {
      s16 LowValue = RegisterBank.Registers[RegIndex];
    }

    *IP += Instruction.Operands[0].Immediate.Value;
  }
  else if (Instruction.Op == Op_test)
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

    s32 DestValue;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      DestValue = Instruction.Operands[0].Immediate.Value;
    }
    else if (Instruction.Operands[0].Type == Operand_Register)
    {
      DestValue = GetRegister(Instruction.Operands[0].Register);
    }

    u16 Result = (u16)DestValue & (u16)SourceValue;

    if (Result > 0)
    {
      SetZFlag();
    }
  }
  else if (Instruction.Op == Op_xor)
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

    s32 DestValue;
    if (Instruction.Operands[0].Type == Operand_Immediate)
    {
      DestValue = Instruction.Operands[0].Immediate.Value;
    }
    else if (Instruction.Operands[0].Type == Operand_Register)
    {
      DestValue = GetRegister(Instruction.Operands[0].Register);
    }
    
    u16 Result = SourceValue ^ DestValue; 
    SetRegister(Instruction.Operands[0].Register, Result);
    ResetFlagsForXOR();
  }
  else 
  {
    // No other instructions have been implemented
    assert(0);
  }

  return &RegisterBank;
}

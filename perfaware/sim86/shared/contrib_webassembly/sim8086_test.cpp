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
  else 
  {
    // No other instructions have been implemented
    assert(0);
  }

  return &RegisterBank;
}

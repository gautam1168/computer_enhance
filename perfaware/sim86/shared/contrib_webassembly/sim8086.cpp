#include "../sim86_shared.h"
#include "../../sim86_memory.h"
#include "../../sim86_decode.h"

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
#define assert(expression) if (!(expression)) { __builtin_trap(); }

struct output_memory
{
  u8 *Base;
  u64 Max; // Bytes
  u64 Used; // Bytes
};

#include "../../sim86_instruction.cpp"
#include "../../sim86_decode.cpp"
#include "../../sim86_memory.cpp"
#include "../../sim86_instruction_table.cpp"

static output_memory OutputMemory;

#include "sim86_text.cpp"

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

static void DisAsm8086Wasm(u32 DisAsmByteCount, segmented_access DisAsmStart, output_memory *OutputMemory)
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
        char LogLine[] = "ERROR: Instruction extends outside disassembly region\n\0";
        PrintToOutput(LogLine, OutputMemory);
        break;
      }
      
      PrintInstruction(Instruction, OutputMemory);

      char LogLine[] = "\n\0";
      PrintToOutput(LogLine, OutputMemory);
    }
    else
    {
      char LogLine[] = "ERROR: Unrecognized binary in instruction stream.\n\0";
      PrintToOutput(LogLine, OutputMemory);
      break;
    }
  }
}

void 
memcpy(u8 *Destination, u8 *Source, u32 SourceSize)
{
	__builtin_memcpy(Destination, Source, SourceSize);
}

extern "C" u32 
Sim86_GetVersion(void)
{
  u32 Result = SIM86_VERSION;
  return Result;
}

extern "C" void 
Sim86_Decode8086Instruction(u32 SourceSize, u8 *Source, instruction *Dest)
{
  instruction_table Table = Get8086InstructionTable();

  // Note left by Casey Muratori. I'm just leaving it as is.
  // NOTE(casey): The 8086 decoder requires the ability to read up to 15 bytes (the maximum
  // allowable instruction size)
  assert(Table.MaxInstructionByteCount == 15);
  u8 GuardBuffer[16] = {};
  if(SourceSize < Table.MaxInstructionByteCount)
  {
    memcpy(GuardBuffer, Source, SourceSize);
    Source = GuardBuffer;
  }

  segmented_access At = FixedMemoryPow2(4, Source);

  *Dest = DecodeInstruction(Table, At);;
}

extern "C" char const *
Sim86_RegisterNameFromOperand(register_access *RegAccess)
{
  char const *Result = GetRegName(*RegAccess);
  return Result;
}

extern "C" char const *
Sim86_MnemonicFromOperationType(operation_type Type)
{
  char const *Result = GetMnemonic(Type);
  return Result;
}

extern "C" void 
Sim86_Get8086InstructionTable(instruction_table *Dest)
{
  *Dest = Get8086InstructionTable();
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
  DisAsm8086Wasm(BytesRead, MainMemory, &OutputMemory);

  // Add the number of bytes to read excluding the number itself
  *OutputSize = OutputMemory.Used;
  return OutputMemory.Base;
}

extern "C" void
TriggerTrap()
{
  assert(0);
}

typedef char unsigned u8;
typedef short unsigned u16;
typedef int unsigned u32;
typedef long long unsigned u64;

typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;

typedef s32 b32;

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
#define assert(expression) if (!(expression)) { __builtin_trap(); }

struct output_memory
{
  u8 *Base;
  u64 Max; // Bytes
  u64 Used; // Bytes
};

static output_memory OutputMemory;

#include "sim86_stdlibfuncs.cpp"
#include "../../sim86_lib.cpp"

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

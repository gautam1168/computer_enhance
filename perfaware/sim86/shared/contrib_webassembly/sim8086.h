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

struct register_bank
{
  u32 CurrentByte;
  u32 CurrentInstruction;
  // AX, BX, CX, DX, SP, BP, SI, DI, ES, CS, SS, DS, IP, FLAGS
  u16 Registers[15];
};

// FLAGS
// _,_,_,_, OF,DF,IF,TF, SF,ZF,_,AF, _,PF,_CF
// OF -> Overflow flag
// SF -> Sign flag
// ZF -> Zero flag (Result of the operation is 0)
// AF -> Auxiliary Carry Flag (Carry from low nibble to high or a borrow)
// PF -> Parity flag is set when the result has an even number of set bits
// CF -> Carry flag is set when there has been a carry or borrow involving the high bit

struct flags {
  u16 OF;
  u16 DF;
  u16 IF;
  u16 TF;
  u16 SF;
  u16 ZF;
  u16 AF;
  u16 PF;
  u16 CF;
};

struct bitwise_add_result
{
  s16 Value;
  bool AuxiliaryCarry;
  bool Carry;
  bool Overflow;
};

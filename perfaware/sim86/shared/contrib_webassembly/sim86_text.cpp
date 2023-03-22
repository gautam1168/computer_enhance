#define FILE int

void
fprintf(FILE *a, const char *b, ...)
{}

#include "../../sim86_text.cpp"

void
ZeroCharBuffer(char Buffer[256])
{
  for (s32 Index = 0; Index < 256; ++Index)
  {
    Buffer[Index] = '\0';
  }
}

inline s32 
NumberOfDigits(s64 Number)
{
  s32 NumDigits = 0;
  if (Number < 0)
  {
    Number *= -1;
  }

  while (Number)
  {
    s32 Digit = (s32)(Number % 10);
    Number = Number / 10;
    NumDigits++;
  }

  return NumDigits;
}

inline void
PutDigitsInBuffer(char Buffer[256], s32 Number, s32 NumDigits, s32 StartIndex = 0)
{
  for (s32 DigitIndex = StartIndex + NumDigits - 1; 
      DigitIndex >= StartIndex;
      --DigitIndex)
  {
    s32 Digit = Number % 10;
    Number = Number / 10;
    Buffer[DigitIndex] = Digit + 48;
  }
}

s32
Interpolate(char Buffer[256], s32 Number, s32 StartIndex = 0)
{
  s32 NumDigits = NumberOfDigits((s64)Number);
  if (Number < 0)
  {
    Number *= -1;
    Buffer[StartIndex++] = '-';
  }

  PutDigitsInBuffer(Buffer, Number, NumDigits, StartIndex);

  return StartIndex + NumDigits;
}

s32
Interpolate(char Buffer[256], u32 Number, s32 StartIndex = 0)
{
  s32 NumDigits = NumberOfDigits((s64)Number);

  PutDigitsInBuffer(Buffer, Number, NumDigits, StartIndex);

  return StartIndex + NumDigits;
}

s32
InterpolateSigned(char Buffer[256], s32 Number, s32 StartIndex = 0)
{
  if (Number < 0)
  {
    Number *= -1;
    Buffer[StartIndex++] = '-';
  } 
  else 
  {
    Buffer[StartIndex++] = '+';
  }

  s32 NumDigits = NumberOfDigits((s64)Number);

  PutDigitsInBuffer(Buffer, Number, NumDigits, StartIndex);

  return StartIndex;
}

s32
InterpolateSigned(char Buffer[256], u32 Number, s32 StartIndex = 0)
{
  Buffer[StartIndex++] = '+';
  s32 NumDigits = NumberOfDigits((s64)Number);

  PutDigitsInBuffer(Buffer, Number, NumDigits, StartIndex);

  return StartIndex;
}

s32
Concat(char Buffer[256], char *Arg1, char *Arg2, s32 StartIndex = 0)
{
  while (*Arg1 && StartIndex < 256)
  {
    Buffer[StartIndex++] = *Arg1++;
  }

  while (*Arg2 && StartIndex < 256)
  {
    Buffer[StartIndex++] = *Arg2++;
  }

  return StartIndex;
}

void
PrintToOutput(char *LogLine, output_memory *OutputBuffer)
{
  if (OutputBuffer->Used + 256 == OutputBuffer->Max)
  {
    OutputBuffer->Used = 0;
  }

  u32 LogLineLength = 0;
  u8 *BuffCursor = OutputBuffer->Base + OutputBuffer->Used;
  // Log lines beyond this will get truncated
  while (LogLineLength < 256)
  {
    if (*LogLine)
    {
      *BuffCursor++ = *LogLine++;
    } 
    else 
    {
      *BuffCursor++ = '\n';
      break;
    }
    LogLineLength++;
  }

  OutputBuffer->Used += LogLineLength;
}

static void 
PrintEffectiveAddressExpressionWasm(effective_address_expression Address, output_memory *Memory)
{
  char const *Separator = "";
  for(u32 Index = 0; Index < ArrayCount(Address.Terms); ++Index)
  {
    effective_address_term Term = Address.Terms[Index];
    register_access Reg = Term.Register;

    if(Reg.Index)
    {
      // fprintf(Dest, "%s", Separator);
      PrintToOutput((char *)Separator, Memory);
      if(Term.Scale != 1)
      {
        char Buffer[256];
        ZeroCharBuffer(Buffer);
        s32 End = Interpolate(Buffer, Term.Scale);
        Buffer[End] = '*';
        PrintToOutput((char *)Buffer, Memory);
        // fprintf(Dest, "%d*", Term.Scale);
      }
      PrintToOutput((char *)GetRegName(Reg), Memory);
      // fprintf(Dest, "%s", GetRegName(Reg));
      Separator = "+";
    }
  }

  if(Address.Displacement != 0)
  {
    char Buffer[256];
    ZeroCharBuffer(Buffer);
    s32 End = InterpolateSigned(Buffer, Address.Displacement);
    PrintToOutput(Buffer, Memory);
    // fprintf(Dest, "%+d", Address.Displacement);
  }
}

static void 
PrintInstructionWasm(instruction Instruction, output_memory *Memory)
{
  u32 Flags = Instruction.Flags;
  u32 W = Flags & Inst_Wide;

  if(Flags & Inst_Lock)
  {
    if(Instruction.Op == Op_xchg)
    {
      // NOTE(casey): This is just a stupidity for matching assembler expectations.
      instruction_operand Temp = Instruction.Operands[0];
      Instruction.Operands[0] = Instruction.Operands[1];
      Instruction.Operands[1] = Temp;
    }
    char const *Literal = "lock ";
    PrintToOutput((char *)Literal, Memory);
    // fprintf(Dest, "lock ");
  }

  char const *MnemonicSuffix = "";
  if(Flags & Inst_Rep)
  {
    char const *Literal = "rep ";
    PrintToOutput((char *)Literal, Memory);
    // fprintf(Dest, "rep ");
    MnemonicSuffix = W ? "w" : "b";
  }

  char Buffer[256];
  ZeroCharBuffer(Buffer);
  s32 End = Concat(Buffer, (char *)GetMnemonic(Instruction.Op), (char *)MnemonicSuffix, 0);
  Buffer[End] = ' ';
  PrintToOutput((char *)Buffer, Memory);
  // fprintf(Dest, "%s%s ", GetMnemonic(Instruction.Op), MnemonicSuffix);

  char const *Separator = "";
  for(u32 OperandIndex = 0; OperandIndex < ArrayCount(Instruction.Operands); ++OperandIndex)
  {
    instruction_operand Operand = Instruction.Operands[OperandIndex];
    if(Operand.Type != Operand_None)
    {
      PrintToOutput((char *)Separator, Memory);
      // fprintf(Dest, "%s", Separator);
      Separator = ", ";

      switch(Operand.Type)
      {
        case Operand_None: {} break;

        case Operand_Register:
         {
           PrintToOutput((char *)GetRegName(Operand.Register), Memory);
           // fprintf(Dest, "%s", GetRegName(Operand.Register));
         } break;

        case Operand_Memory:
         {
           effective_address_expression Address = Operand.Address;

           if(Flags & Inst_Far)
           {
             char Literal[] = "far \0";
             PrintToOutput(Literal, Memory);
             // fprintf(Dest, "far ");
           }

           if(Address.Flags & Address_ExplicitSegment)
           {
             char Buffer[256];
             ZeroCharBuffer(Buffer);
             s32 End = Interpolate(Buffer, Address.ExplicitSegment);
             End = Concat(Buffer, (char *)"", (char *)":", End);
             End = Interpolate(Buffer, Address.Displacement, End);
             PrintToOutput(Buffer, Memory);
             // fprintf(Dest, "%u:%u", Address.ExplicitSegment, Address.Displacement);
           }
           else
           {
             if(Instruction.Operands[0].Type != Operand_Register)
             {
               if (W) 
               {
                 PrintToOutput((char *)"word ", Memory);
               }
               else 
               {
                 PrintToOutput((char *)"byte ", Memory);
               }
               // fprintf(Dest, "%s ", W ? "word" : "byte");
             }

             if(Flags & Inst_Segment)
             {
               char Buffer[256];
               ZeroCharBuffer(Buffer);
               Concat(Buffer, (char *)(GetRegName({Instruction.SegmentOverride, 0, 2})), (char *)":");
               PrintToOutput(Buffer, Memory);
               // fprintf(Dest, "%s:", GetRegName({Instruction.SegmentOverride, 0, 2}));
             }

             PrintToOutput((char *)"[", Memory);
             // fprintf(Dest, "[");
             PrintEffectiveAddressExpressionWasm(Address, Memory);
             PrintToOutput((char *)"]", Memory);
             // fprintf(Dest, "]");
           }
         } break;

        case Operand_Immediate:
         {
           immediate Immediate = Operand.Immediate;
           if(Immediate.Flags & Immediate_RelativeJumpDisplacement)
           {
             char Buffer[256];
             ZeroCharBuffer(Buffer);
             Buffer[0] = '$';
             Interpolate(Buffer, Immediate.Value + Instruction.Size, 1);
             // fprintf(Dest, "$%+d", Immediate.Value + Instruction.Size);
           }
           else
           {
             char Buffer[256];
             ZeroCharBuffer(Buffer);
             Interpolate(Buffer, Immediate.Value);
             PrintToOutput(Buffer, Memory);
             // fprintf(Dest, "%d", Immediate.Value);
           }
         } break;
      }
    }
  }
}

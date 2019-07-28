#include <Arduino.h>
#include "CPU.h"
#include "Config.h" // added for m_bKenbakExt

// Kenbak-uino
// The "CPU"

/*
the op-codes:
----Bit Test & Manipulate----   R == 2
P--           -Q-         --R
0 = Set 0     0
1 = Set 1     1
2 = Skip 0    2           2
3 = Skip 1    3
              4
              5
              6
              7 = bit number
 
----Shifts/Rotates----          R == 1
P--           -Q-         --R
0 = Rt Shift  0 = A:4
1 = Rt Rot    1 = A:1     1
2 = Lt Shift  2 = A:2
3 = Lt Rot    3 = A:3
              4 = B:4
              5 = B:1
              6 = B:2
              7 = B:3
 
----Misc----                    R == 0
P--           -Q-         --R
0 = Halt      0           0
1 = Halt      1
2 = NOP       2
3 = NOP       3
              4
              5
              6
              7 = don't care
 
----Jumps----                   Q >= 4
P--           -Q-         --R
0 = A
1 = B
2 = X
3 = Unc                   3 != 0
              4 = JPD     4 == 0
              5 = JPI     5 =< 0
              6 = JMD     6 >= 0
              7 = JMI     7 > 0
 
----Or, And, LNeg----          P == 3
P--           -Q-         --R
3             0 = Or
              1 = Load A current page (was NOP)
              2 = And
              3 - LNeg    3 = Const
                          4 = Mem
                          5 = Ind
                          6 = Idx
                          7 = Ind/Idx

----Add, Sub, Load, Store----
P--           -Q-         --R
0 = A         0 = Add        
1 = B         1 = Sub
2 = X         2 = Load
              3 = Store   3 = Const
                          4 = Mem
                          5 = Ind
                          6 = Idx
                          7 = Ind/Idx
*/

#define OP_MODE_CONST      3 // or Immediate (Store)
#define OP_MODE_MEM        4
#define OP_MODE_INDIRECT   5
#define OP_MODE_INDEXED    6
#define OP_MODE_INDIND     7 

#define OP_TEST_NE   3
#define OP_TEST_EQ   4
#define OP_TEST_LT   5
#define OP_TEST_GE   6
#define OP_TEST_GT   7


CPU* CPU::cpu = NULL;

CPU::CPU(void)
{
  cpu = this;
}


void CPU::Init()
{
}

byte CPU::Read(word Addr)
{
  // get the byte at Addr
  return m_Memory[Addr];
}

void CPU::Write(word Addr, byte Value)
{
  // set the byte at Addr
  m_Memory[Addr] = Value;
}


byte CPU::GetBitField(byte Byte, int BitNum, int NumBits)
{
  // a helper -- get a bit field from the Byte
  return (Byte >> BitNum) & (0xFF >> (8 - NumBits));
}

byte* CPU::GetAddr(byte* pByte, byte Mode, bool IndPaged)
{
  // do the addressing modes -- all memory references except IndPaged are to page 0
  // calculate indirect address if needed -- if using PC, will go to current page
  word current_page = ((word)m_Memory[REG_FLAGS_X_IDX] << 2) & 0x300;
  word indirectAddr;
  if (*pByte==REG_P_IDX)
  {
    indirectAddr = current_page + m_Memory[REG_P_IDX]; 
  }
  else
  {
    indirectAddr = m_Memory[*pByte];  
  }
  switch (Mode)
  {
    case OP_MODE_MEM:      
      if (IndPaged)   // used to get indirect address from beginning of a subroutine on current page
      {
        return m_Memory + current_page + (*pByte); 
      }
      else
      {
        return m_Memory + (*pByte);
      }
    case OP_MODE_INDIRECT: return m_Memory + indirectAddr;
    case OP_MODE_INDEXED:  return m_Memory + (byte)((*pByte) + m_Memory[REG_X_IDX]);
    case OP_MODE_INDIND:   return m_Memory + (byte)(indirectAddr + m_Memory[REG_X_IDX]);
    default:               return pByte; // OP_MODE_CONST or other
  }
}

// added for Kenbak-1K: special version for Load A current page
byte* CPU::GetAddrCP(byte* pByte, byte Mode)
{
  // do the addressing modes
  // regular address and address,indexed uses current page instead of page 0
  // indirect uses indirect address on page 0 as pointer to address on current page
  
  word current_page = ((word)m_Memory[REG_FLAGS_X_IDX] << 2) & 0x300;
  switch (Mode)
  {
    case OP_MODE_MEM: return m_Memory + current_page + (*pByte);
    case OP_MODE_INDIRECT: return m_Memory + current_page + m_Memory[*pByte];
    case OP_MODE_INDEXED:  return m_Memory + current_page + (byte)((*pByte) + m_Memory[REG_X_IDX]);
    case OP_MODE_INDIND:   return m_Memory + current_page + (byte)(m_Memory[*pByte] + m_Memory[REG_X_IDX]);
    default:               return pByte; // OP_MODE_CONST or other
  }
}

void CPU::ClearAllMemory()
{
  memset(m_Memory, 0, 1024);    // was 256 for original Kenbak-1
}

bool CPU::OnNOOPExtension(byte )
{
  // by default a NOOP does nothing (but says "keep running")
  // this can be over-written.  return false to HALT
  return true;
}

byte* CPU::Memory()
{
  // a pointer to the 1024 bytes of memory
  return m_Memory;
}

byte* CPU::GetNextByte()
{
  // get extended prog ctr consisting of 8-bit REG_P_IDX prepended by two page bits at top of REG_FLAGS_X_IDX
  word pc = (((word)m_Memory[REG_FLAGS_X_IDX] << 2) & 0x300) + (word)m_Memory[REG_P_IDX];
  m_Memory[REG_P_IDX]++;    // incr original pc -- note: at 0377, wraps around; does not incr to next oage
  return m_Memory + pc;     // return pointer into memory array
}

bool CPU::Step()
{
  // one instruction, false means HALT
  return Execute(*GetNextByte());
}

bool CPU::Execute(byte Instruction)
{
  // decode/execute Instruction, false means HALT, processes the next byte if required
  byte P__ = GetBitField(Instruction, 6, 2);
  byte _Q_ = GetBitField(Instruction, 3, 3);
  byte __R = GetBitField(Instruction, 0, 3);

  if (__R == 0)  // ==================== Miscellaneous (one byte only)
  {
    if (P__ == 0 || P__ == 1)  // HALT
      return false;
    else // NOOP
      if (_Q_ != 0)
        return OnNOOPExtension(Instruction);
  }
  else if (__R == 1)  // ==================== Shifts, Rotates (one byte only)
  {
    byte Places = _Q_ & 0x03;
    byte Rotate = P__ & 0x01;
    byte Left   = P__ & 0x02;
    byte* pValue = m_Memory + ((_Q_ & 0x04)?REG_B_IDX:REG_A_IDX);
    if (Places == 0) 
      Places = 4;

    if (Left) // left
    {
      byte Rot = *pValue & 0x80;  // grab that bit
      *pValue <<= Places;         // shift
      if (Rotate && Rot)          // or-in the bit that rolled off
        *pValue |= 0x01;
    }
    else  // right
    {
      byte Rot = *pValue & 0x01;  // grab that bit
      *pValue >>= Places;         // shift
      if (Rotate && Rot)          // or-in the bit that rolled off
        *pValue |= 0x80;
    }
  }
  else if (__R == 2)  // ==================== Bit Test and Manipulation
  {
    byte Mask = 0x01 << _Q_;
    byte* Addr = GetAddr(GetNextByte(), OP_MODE_MEM, false);
    byte One = P__ & 0x01;
    if (P__ & 0x02) // SKIP
    {
      byte Skip;
      if (One)
        Skip = *Addr & Mask;
      else
        Skip = !(*Addr & Mask);
      if (Skip) // skip the next instruction (2 bytes)
        m_Memory[REG_P_IDX] += 2;
    }
    else  // SET
    {
      if (One)
        *Addr |= Mask;
      else
        *Addr &= ~Mask;
    }
  }
  else if (_Q_ > 3)    // ==================== jumps
  {
    byte JumpAndMark = _Q_ & 0x02;
    byte AddressMode = (_Q_ & 0x01) + OP_MODE_CONST;
    byte TestByte = m_Memory[P__];
    byte Condition = 0;
    byte TargetAddr = *GetAddr(GetNextByte(), AddressMode, true);

    if (P__ == 3)
    {
      Condition = 1;
      if (__R == OP_TEST_NE)
      {
        __R = OP_TEST_EQ;     // change any 0343 opcodes to 0344 so they will reflect page 0  
      }
    }
    else if (__R == OP_TEST_NE)
      Condition = TestByte;
    else if (__R == OP_TEST_EQ)
      Condition = !TestByte;
    else if (__R == OP_TEST_LT)
      Condition = TestByte & 0x80;
    else if (__R == OP_TEST_GE)
      Condition = !(TestByte & 0x80) || (TestByte == 0);
    else if (__R == OP_TEST_GT)
      Condition = !(TestByte & 0x80) && (TestByte != 0);

    if (Condition)
    {
      if (JumpAndMark)
      {
        if (config.m_bKenbakExt)  // if extensions enabled, need to store return address on current page, not always page 0
        {        
          word ExtTargetAddr = (((word)m_Memory[REG_FLAGS_X_IDX] << 2) & 0x300) + TargetAddr;
          m_Memory[ExtTargetAddr] = m_Memory[REG_P_IDX];   // saves just low 8-bits; current page assumed on return
          TargetAddr++;
        }
        else
        {
	        m_Memory[TargetAddr] = m_Memory[REG_P_IDX];
	        TargetAddr++;          
        }
      }
      // if extensions enabled, unconditional jump but not a JumpAndMark, and not indirect, then will jump to new page
      if (config.m_bKenbakExt && (P__ == 3) && !(_Q_ & 0x01) && !JumpAndMark)
      {
        // set page bits from low two bits of instruction while preserving existing flags
        // next instruction will be fetched from new page
        m_Memory[REG_FLAGS_X_IDX] = (__R << 6) | (m_Memory[REG_FLAGS_X_IDX] & 0x03);  
      }      
      m_Memory[REG_P_IDX] = TargetAddr;   // low 8 bits of PC
    }
  }
  else if (P__ == 3)  // ==================== Or, And, Lneg, Noop (or Load A current page)
  {
    byte* regA = m_Memory + REG_A_IDX;
    if (_Q_ == 1) // (NOP or Load A current page)
    {
      if (config.m_bKenbakExt)  // if extensions enabled, then this is a Load A current page instr
      {
        byte* operand = GetAddrCP(GetNextByte(), __R);
        *regA = *operand;         
      }
      else
      {
        return OnNOOPExtension(Instruction);
      }
    }
    else
    {    
      byte* operand = GetAddr(GetNextByte(), __R, false);
      if (_Q_ == 0)  // OR
        *regA |= *operand;
      else if (_Q_ == 2) // AND
        *regA &= *operand;
      else if (_Q_ == 3) // LNEG
      {
        signed char Temp = -(signed char)(*operand);
        *regA = (byte)Temp;
        // flags are not set
      }
    }
  }
  else  // ==================== Add, Sub, Load, Store
  {
    byte* pLHS = m_Memory + P__;
    byte* pFlags = m_Memory + REG_FLAGS_A_IDX + P__;
    byte* pRHS = GetAddr(GetNextByte(), __R, false);
    word LHS = *pLHS;
    word RHS = *pRHS;
    word Result;
    if (_Q_ == 0) // ADD
    {
      Result = LHS + RHS;
      *pLHS = (byte)Result;
      *pFlags  &= ~0x03;  // only clear out bottom two bits; top two bits of X reg used as page #
      if (Result & 0xFF00)
        *pFlags |= 0x02; // carry

      signed short int Temp = (signed char)LHS + (signed char)RHS;
      if (Temp < -128 || Temp > 127)
        *pFlags |= 0x01; // overflow
    }
    else if (_Q_ == 1)  // SUB
    {
      Result = LHS - RHS;
      *pLHS = (byte)Result;
      *pFlags  &= ~0x03;  // only clear out bottom two bits; top two bits of X reg used as page #
      if (Result & 0xFF00)
        *pFlags |= 0x02; // carry (borrow)

      signed short int Temp = (signed char)LHS - (signed char)RHS;
      if (Temp < -128 || Temp > 127)
        *pFlags |= 0x01; // overflow
    }
    else if (_Q_ == 2)  // LOAD
      *pLHS = *pRHS; 
    else if (_Q_ == 3)  // STORE
      *pRHS = *pLHS;
  }
  return true;
}

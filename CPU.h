#ifndef cpu_h
#define cpu_h

// emulates extended KENBAK-1 "cpu" with optional 1K of RAM instead of 256 bytes

#define REG_A_IDX       000
#define REG_B_IDX       001
#define REG_X_IDX       002
#define REG_P_IDX       003

#define REG_OUTPUT_IDX  0200
#define REG_FLAGS_A_IDX 0201
#define REG_FLAGS_B_IDX 0202
#define REG_FLAGS_X_IDX 0203  // also has page # in upper two bits
#define REG_INPUT_IDX   0377


class CPU
{
public:
  CPU(void);

  virtual void Init();
  virtual bool Step();
  byte Read(word Addr);
  void Write(word Addr, byte Value);
  void ClearAllMemory();
  virtual bool OnNOOPExtension(byte Op);

  byte* Memory();

  static CPU* cpu;

private:
  byte GetBitField(byte Byte, int BitNum, int NumBits);
  byte* GetAddr(byte* pByte, byte Mode, bool IndPaged);
  byte* GetAddrCP(byte* pByte, byte Mode);  // added for Kenbak-1K
  byte* GetNextByte();
  bool Execute(byte Instruction);

  byte m_Memory[1024];
};

#endif

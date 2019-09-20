#include <base.h>
#include <membus.h>
#include <mos6502/mos6502.h>
#include <mos6502/vmcall.h>
#include <rc.h>
#include <timekeeper.h>

#include <string.h>

static const uint8_t instr_cycles[256] = {
    7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6,
    2, 4, 2, 7, 4, 4, 7, 7, 6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7, 6, 6, 2, 8, 3, 3, 5, 5,
    3, 2, 2, 2, 3, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6,
    2, 4, 2, 7, 4, 4, 7, 7, 6, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5, 2, 6, 2, 6, 3, 3, 3, 3,
    2, 2, 2, 2, 4, 4, 4, 4, 2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6,
    2, 4, 2, 7, 4, 4, 7, 7, 2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
};

typedef struct {
  uint8_t valid;
  enum addr_mode mode;
  uint8_t opcode;
  union {
    uint8_t arg8;
    uint16_t arg16;
  };
  uint16_t abs_addr;
} enc_t;
// TODO: determine what a widget function is
//      -- nick
typedef void (*widget_func_t)(mos6502_t*, enc_t*);

/**
 * An widget contains information about how to execute an instruction
 *      -- nick
 */
typedef struct {
  uint8_t valid;  // default to 0, I think, 1 is valid
  const char* name;
  enum addr_mode mode;
  widget_func_t evaluator;
} widget_t;

static const widget_t widgets[];

static inline uint8_t read8(mos6502_t* cpu, uint16_t addr) {
  return membus_read(cpu->bus, addr);
}

static inline void write8(mos6502_t* cpu, uint16_t addr, uint8_t val) {
  membus_write(cpu->bus, addr, val);
}

static inline uint16_t read16(mos6502_t* cpu, uint16_t addr) {
  uint16_t lo = (uint16_t)read8(cpu, addr);
  uint16_t hi = (uint16_t)read8(cpu, addr + 1);
  uint16_t val = lo | (uint16_t)(hi << 8);
  return val;
}

static inline uint16_t buggy_read16(mos6502_t* cpu, uint16_t addr) {
  uint16_t first = addr;
  uint16_t secnd = (addr & 0xFF00) | (uint16_t)((uint8_t)addr + 1);

  uint16_t lo = (uint16_t)read8(cpu, first);
  uint16_t hi = (uint16_t)read8(cpu, secnd);
  uint16_t val = (uint16_t)(hi << 8) | lo;

  return val;
}

size_t mos6502_instr_repr(mos6502_t* cpu, uint16_t addr, char* buffer,
                          size_t buflen) {
  // FILL ME IN

  // Delete this line when you're done
  buffer[0] = 0;
  return 0;
}

static int decode(mos6502_t* cpu, int pc, enc_t* enc) {
  enc->valid = 1;
  enc->opcode = read8(cpu, cpu->pc);
  const widget_t* w = &widgets[enc->opcode];
  if (w->valid != 1) {
    enc->valid = 0;
    return 0;
  }

  enc->mode = w->mode;
  switch (w->mode) {
    case MODE_NONE:
      // No address calculation in decode
      enc->valid = 0;
      return pc;

    case MODE_ABS:
      enc->abs_addr = enc->arg16 = read16(cpu, pc + 1);
      return pc + 2;

    case MODE_ABSX:
      enc->arg16 = read16(cpu, pc + 1);
      enc->abs_addr = enc->arg16 + cpu->x;
      return pc + 2;

    case MODE_ABSY:
      enc->arg16 = read16(cpu, pc + 1);
      enc->abs_addr = enc->arg16 + cpu->y;
      return pc + 2;

    case MODE_ACC:
      // Accumulator
      return pc + 1;

    case MODE_IMM:
      // Immediate
      enc->arg8 = read8(cpu, pc + 1);
      return pc + 2;

    case MODE_IMPL:
      // Implied
      return pc + 1;

    case MODE_IND:
      // the address here is the one that is indirectly pointed to
      enc->arg16 = read16(cpu, pc+1);
      enc->abs_addr = buggy_read16(cpu, enc->arg16);
      return pc + 3;

    case MODE_XIND:
      // the supplied 8-bit address is offset by X reg to index a location in page 0x00
      // the actual address is read from this location
      enc->arg8 = read8(cpu, pc+1);
      // read the actual address
      enc->abs_addr = read16(cpu, enc->arg8 + cpu->x);
      return pc + 2;
      break;

    case MODE_INDY:
      // Indirect-indexed
      enc->arg8 = read8(cpu, pc+1);
      // read the actual address
      enc->abs_addr = read16(cpu, enc->arg8) + cpu->y;
      return pc + 2;

    case MODE_REL:
      // Relative
      enc->arg8 = read8(cpu, pc + 1);
      return pc + 2;

    case MODE_ZEROP:
      // Zero-page operations let you read a single byte from the first page.
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = enc->arg16; // some trickery
      return pc + 2;

    case MODE_ZEROPX:
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = enc->arg16 + cpu->x;
      return pc + 2;

    case MODE_ZEROPY:
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = enc->arg16 + cpu->y;
      return pc + 2;
  }

  return pc;
}

mos6502_step_result_t mos6502_step(mos6502_t* cpu) {
  enc_t enc;
  int newpc = decode(cpu, cpu->pc, &enc);
  if (enc.valid != 1) {
    return MOS6502_STEP_RESULT_ILLEGAL_INSTRUCTION;
  }

  // evaluate
  widgets[enc.opcode].evaluator(cpu, &enc);
  cpu->pc = newpc;

  mos6502_advance_clk(cpu, instr_cycles[enc.opcode]);
  return MOS6502_STEP_RESULT_SUCCESS;
}

#define NOT_IMPLEMENTED(name)           \
  {                                     \
    printf(#name " NOT IMPLEMENTED\n"); \
    while (1)                           \
      ;                                 \
  }
#define defop(name) void eval_##name(mos6502_t* cpu, enc_t* enc)

defop(ADC) { NOT_IMPLEMENTED(ADC); }
defop(AND) { NOT_IMPLEMENTED(AND); }
defop(ASL) { NOT_IMPLEMENTED(ASL); }
defop(BCC) { NOT_IMPLEMENTED(BCC); }
defop(BCS) { NOT_IMPLEMENTED(BCS); }
defop(BEQ) { NOT_IMPLEMENTED(BEQ); }
defop(BIT) { NOT_IMPLEMENTED(BIT); }
defop(BMI) { NOT_IMPLEMENTED(BMI); }
defop(BNE) { NOT_IMPLEMENTED(BNE); }
defop(BPL) { NOT_IMPLEMENTED(BPL); }
defop(BRK) { NOT_IMPLEMENTED(BRK); }
defop(BVC) { NOT_IMPLEMENTED(BVC); }
defop(BVS) { NOT_IMPLEMENTED(BVS); }
defop(CLC) { NOT_IMPLEMENTED(CLC); }
defop(CLD) { NOT_IMPLEMENTED(CLD); }
defop(CLI) { NOT_IMPLEMENTED(CLI); }
defop(CLV) { NOT_IMPLEMENTED(CLV); }
defop(CMP) { NOT_IMPLEMENTED(CMP); }
defop(CPX) { NOT_IMPLEMENTED(CPX); }
defop(CPY) { NOT_IMPLEMENTED(CPY); }
defop(DEC) { NOT_IMPLEMENTED(DEC); }
defop(DEX) { NOT_IMPLEMENTED(DEX); }
defop(DEY) { NOT_IMPLEMENTED(DEY); }
defop(EOR) { NOT_IMPLEMENTED(EOR); }
defop(INC) { NOT_IMPLEMENTED(INC); }
defop(INX) { NOT_IMPLEMENTED(INX); }
defop(INY) { NOT_IMPLEMENTED(INY); }
defop(JMP) { NOT_IMPLEMENTED(JMP); }
defop(JSR) { NOT_IMPLEMENTED(JSR); }
defop(LDA) { NOT_IMPLEMENTED(LDA); }
defop(LDX) { NOT_IMPLEMENTED(LDX); }
defop(LDY) { NOT_IMPLEMENTED(LDY); }
defop(LSR) { NOT_IMPLEMENTED(LSR); }
defop(NOP) { NOT_IMPLEMENTED(NOP); }
defop(ORA) { NOT_IMPLEMENTED(ORA); }
defop(PHA) { NOT_IMPLEMENTED(PHA); }
defop(PHP) { NOT_IMPLEMENTED(PHP); }
defop(PLA) { NOT_IMPLEMENTED(PLA); }
defop(PLP) { NOT_IMPLEMENTED(PLP); }
defop(ROL) { NOT_IMPLEMENTED(ROL); }
defop(ROR) { NOT_IMPLEMENTED(ROR); }
defop(RTI) { NOT_IMPLEMENTED(RTI); }
defop(RTS) { NOT_IMPLEMENTED(RTS); }
defop(SBC) { NOT_IMPLEMENTED(SBC); }
defop(SEC) { NOT_IMPLEMENTED(SEC); }
defop(SED) { NOT_IMPLEMENTED(SED); }
defop(SEI) { NOT_IMPLEMENTED(SEI); }
defop(STA) { NOT_IMPLEMENTED(STA); }
defop(STX) { NOT_IMPLEMENTED(STX); }
defop(STY) { NOT_IMPLEMENTED(STY); }
defop(TAX) { NOT_IMPLEMENTED(TAX); }
defop(TAY) { NOT_IMPLEMENTED(TAY); }
defop(TSX) { NOT_IMPLEMENTED(TSX); }
defop(TXA) { NOT_IMPLEMENTED(TXA); }
defop(TXS) { NOT_IMPLEMENTED(TXS); }
defop(TYA) { NOT_IMPLEMENTED(TYA); }

#define O(opname, opmode)                               \
  {                                                     \
    .valid = 1, .name = #opname, .mode = MODE_##opmode, \
    .evaluator = eval_##opname                          \
  }

// built from https://www.masswerk.at/6502/6502_instruction_set.html#RTI
static const widget_t widgets[256] = {
    // first column: X0
    [0x00] = O(BRK, IMPL),
    [0x10] = O(BPL, REL),
    [0x20] = O(JSR, ABS),
    [0x30] = O(BMI, REL),
    [0x40] = O(RTI, IMPL),
    [0x50] = O(BVC, REL),
    [0x60] = O(RTS, IMPL),
    [0x70] = O(BVS, REL),
    [0x90] = O(BCC, REL),
    [0xA0] = O(LDY, IMM),
    [0xB0] = O(BCS, REL),
    [0xC0] = O(CPY, IMM),
    [0xD0] = O(BNE, REL),
    [0xE0] = O(CPX, IMM),
    [0xF0] = O(BEQ, REL),

    // second column: X1
    [0x01] = O(ORA, XIND),
    [0x11] = O(ORA, INDY),
    [0x21] = O(AND, XIND),
    [0x31] = O(AND, INDY),
    [0x41] = O(EOR, XIND),
    [0x51] = O(EOR, INDY),
    [0x61] = O(ADC, XIND),
    [0x71] = O(ADC, INDY),
    [0x81] = O(STA, XIND),
    [0x91] = O(STA, INDY),
    [0xA1] = O(LDA, XIND),
    [0xB1] = O(LDA, INDY),
    [0xC1] = O(CMP, XIND),
    [0xD1] = O(CMP, INDY),
    [0xE1] = O(SBC, XIND),
    [0xF1] = O(SBC, INDY),

    // third column: X2
    [0xA2] = O(LDX, IMM),

    // fourth column: X3
    // NONE

    // fifth column: X4
    [0x24] = O(BIT, ZEROP),
    [0x84] = O(STY, ZEROP),
    [0x94] = O(STY, ZEROPX),
    [0xA4] = O(LDY, ZEROP),
    [0xB4] = O(LDY, ZEROPX),
    [0xC4] = O(CPY, ZEROP),
    [0xE4] = O(CPX, ZEROP),

    // sixth column: X5
    [0x05] = O(ORA, ZEROP),
    [0x15] = O(ORA, ZEROPX),
    [0x25] = O(AND, ZEROP),
    [0x35] = O(AND, ZEROPX),
    [0x45] = O(EOR, ZEROP),
    [0x55] = O(EOR, ZEROPX),
    [0x65] = O(ADC, ZEROP),
    [0x75] = O(ADC, ZEROPX),
    [0x85] = O(STA, ZEROP),
    [0x95] = O(STA, ZEROPX),
    [0xA5] = O(LDA, ZEROP),
    [0xB5] = O(LDA, ZEROPX),
    [0xC5] = O(CMP, ZEROP),
    [0xD5] = O(CMP, ZEROPX),
    [0xE5] = O(SBC, ZEROP),
    [0xF5] = O(SBC, ZEROPX),

    // Seventh column: X6
    [0x06] = O(ASL, ZEROP),
    [0x16] = O(ASL, ZEROPX),
    [0x26] = O(ROL, ZEROP),
    [0x36] = O(ROL, ZEROPX),
    [0x46] = O(LSR, ZEROP),
    [0x56] = O(LSR, ZEROPX),
    [0x66] = O(ROR, ZEROP),
    [0x76] = O(ROR, ZEROPX),
    [0x86] = O(STX, ZEROP),
    [0x96] = O(STX, ZEROPX),
    [0xA6] = O(LDX, ZEROP),
    [0xB6] = O(LDX, ZEROPX),
    [0xC6] = O(DEC, ZEROP),
    [0xD6] = O(DEC, ZEROPX),
    [0xE6] = O(INC, ZEROP),
    [0xF6] = O(INC, ZEROPX),

    // Eighth column: X7
    // NONE

    // ninth column: X8
    [0x08] = O(PHP, IMPL),
    [0x18] = O(CLC, IMPL),
    [0x28] = O(PLP, IMPL),
    [0x38] = O(SEC, IMPL),
    [0x48] = O(PHA, IMPL),
    [0x58] = O(CLI, IMPL),
    [0x68] = O(PLA, IMPL),
    [0x78] = O(SEI, IMPL),
    [0x88] = O(DEY, IMPL),
    [0x98] = O(TYA, IMPL),
    [0xA8] = O(TAY, IMPL),
    [0xB8] = O(CLV, IMPL),
    [0xC8] = O(INY, IMPL),
    [0xD8] = O(CLD, IMPL),
    [0xE8] = O(INX, IMPL),
    [0xF8] = O(SED, IMPL),

    // tenth column: X9
    [0x09] = O(ORA, IMM),
    [0x19] = O(ORA, ABSY),
    [0x29] = O(AND, IMM),
    [0x39] = O(AND, ABSY),
    [0x49] = O(EOR, IMM),
    [0x59] = O(EOR, ABSY),
    [0x69] = O(ADC, IMM),
    [0x79] = O(ADC, ABSY),
    [0x99] = O(STA, ABSY),
    [0xA9] = O(LDA, IMM),
    [0xB9] = O(LDA, ABSY),
    [0xC9] = O(CMP, IMM),
    [0xD9] = O(CMP, ABSY),
    [0xE9] = O(SBC, IMM),
    [0xF9] = O(SBC, ABSY),

    // 11th column: XA
    [0x0A] = O(ASL, ACC),
    [0x2A] = O(ROL, ACC),
    [0x4A] = O(LSR, ACC),
    [0x6A] = O(ROR, ACC),
    [0x8A] = O(TXA, IMPL),
    [0x9A] = O(TXS, IMPL),
    [0xAA] = O(TAX, IMPL),
    [0xBA] = O(TSX, IMPL),
    [0xCA] = O(DEX, IMPL),
    [0xEA] = O(NOP, IMPL),

    // nothing in the 12th column: XB

    // 13th column: XC
    [0x2C] = O(BIT, ABS),
    [0x4C] = O(JMP, ABS),
    [0x6C] = O(JMP, IND),
    [0x8C] = O(STY, ABS),
    [0xAC] = O(LDY, ABS),
    [0xBC] = O(LDY, ABSX),
    [0xCC] = O(CPY, ABS),
    [0xEC] = O(CPX, ABS),

    // 14th column: XD
    [0x0D] = O(ORA, ABS),
    [0x1D] = O(ORA, ABSX),
    [0x2D] = O(AND, ABS),
    [0x3D] = O(AND, ABSX),
    [0x4D] = O(EOR, ABS),
    [0x5D] = O(EOR, ABSX),
    [0x6D] = O(ADC, ABS),
    [0x7D] = O(ADC, ABSX),
    [0x8D] = O(STA, ABS),
    [0x9D] = O(STA, ABSX),
    [0xAD] = O(LDA, ABS),
    [0xBD] = O(LDA, ABSX),
    [0xCD] = O(CMP, ABS),
    [0xDD] = O(CMP, ABSX),
    [0xED] = O(SBC, ABS),
    [0xFD] = O(SBC, ABSX),

    // 15th column: XE
    [0x0E] = O(ASL, ABS),
    [0x1E] = O(ASL, ABSX),
    [0x2E] = O(ROL, ABS),
    [0x3E] = O(ROL, ABSX),

    [0x4E] = O(LSR, ABS),
    [0x5E] = O(LSR, ABSX),

    [0x6E] = O(ROR, ABS),
    [0x7E] = O(ROR, ABSX),

    [0x8E] = O(STX, ABS),

    [0xAE] = O(LDX, ABS),
    [0xBE] = O(LDX, ABSY),

    [0xCE] = O(DEC, ABS),
    [0xDE] = O(DEC, ABSX),
    [0xEE] = O(INC, ABS),
    [0xFE] = O(INC, ABSX),

};
#undef O

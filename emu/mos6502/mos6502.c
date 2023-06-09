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

  // some instructions conditionally have more clock cycles
  uint16_t more_clk;
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
  uint16_t secnd = (addr & 0xFF00);  // | (uint16_t)((uint8_t)addr + 1);

  uint16_t lo = (uint16_t)read8(cpu, first);
  uint16_t hi = (uint16_t)read8(cpu, secnd);
  uint16_t val = (uint16_t)(hi << 8) | lo;

  return val;
}

static int decode(mos6502_t* cpu, int pc, enc_t* enc) {
  // start out with more_clk as zero
  enc->more_clk = 0;
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
      return pc + 3;

    case MODE_ABSX:
      enc->arg16 = read16(cpu, pc + 1);
      enc->abs_addr = enc->arg16 + cpu->x;
      return pc + 3;

    case MODE_ABSY:
      enc->arg16 = read16(cpu, pc + 1);
      enc->abs_addr = enc->arg16 + cpu->y;
      return pc + 3;

    case MODE_ACC:
      // Accumulator
      return pc + 1;

    case MODE_IMM:
      // Immediate
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = pc + 1;
      return pc + 2;

    case MODE_IMPL:
      // Implied
      return pc + 1;

    case MODE_IND:

    {
      uint16_t plo = read8(cpu, pc + 1);
      uint16_t phi = read8(cpu, pc + 2);

      uint16_t ptr = (phi << 8) | plo;

      enc->arg16 = ptr;

      // oof, a bug
      if (plo == 0xFF) {
        // fprintf(stderr, "BUG!\n");
        enc->abs_addr = buggy_read16(cpu, ptr);
        // fprintf(stderr, "abs = %04x\n", enc->abs_addr);
      } else {
        // fprintf(stderr, "NO BUG!\n");
        enc->abs_addr = read16(cpu, ptr);
      }
      return pc + 3;
    }

    case MODE_XIND:
      // the supplied 8-bit address is offset by X reg to index a location in
      // page 0x00 the actual address is read from this location
      enc->arg8 = read8(cpu, pc + 1);
      // read the actual address
      enc->abs_addr = read16(cpu, enc->arg8 + cpu->x);
      return pc + 2;

    case MODE_INDY:
      // Indirect-indexed
      enc->arg8 = read8(cpu, pc + 1);
      // read the actual address
      enc->abs_addr = read16(cpu, enc->arg8) + cpu->y;
      return pc + 2;

    case MODE_REL:
      // Relative
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = ((char)enc->arg8) + pc + 2;
      // fprintf(stderr, "%d\n", enc->arg8);
      return pc + 2;

case MODE_ZEROP:
      // Zero-page operations let you read a single byte from the first page.
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = enc->arg8 & 0xFF;  // some trickery
      enc->abs_addr &= 0xFF;
      return pc + 2;

    case MODE_ZEROPX:
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = (enc->arg8 & 0xFF) + cpu->x;  // some trickery
      enc->abs_addr &= 0xFF;
      return pc + 2;

    case MODE_ZEROPY:
      enc->arg8 = read8(cpu, pc + 1);
      enc->abs_addr = (enc->arg8 & 0xFF) + cpu->y;  // some trickery
      enc->abs_addr &= 0xFF;
      return pc + 2;
  }

  return pc;
}

// TODO: THIS FUNCTION WILL READ FROM MEMORY, WHICH MEANS IT HAS SIDEEFFECTS
//       like if it reads from a memory device or something...
size_t mos6502_instr_repr(mos6502_t* cpu, uint16_t addr, char* buffer,
                          size_t buflen) {
  buffer[0] = 0;

  enc_t e;
  decode(cpu, addr, &e);

  if (!e.valid) return 0;

  // no need to check validity, decode does that
  const widget_t* w = &widgets[e.opcode];

  switch (e.mode) {
    case MODE_NONE:
      return 0;

    case MODE_ABS:
      return snprintf(buffer, buflen, "%s  $%04x", w->name, e.arg16);

    case MODE_ABSX:
      return snprintf(buffer, buflen, "%s  $%04x,X", w->name, e.arg16);

    case MODE_ABSY:
      return snprintf(buffer, buflen, "%s  $%04x,Y", w->name, e.arg16);

    case MODE_ACC:
      return snprintf(buffer, buflen, "%s  A", w->name);

    case MODE_IMM:
      return snprintf(buffer, buflen, "%s  #$%02x", w->name,
                      read8(cpu, e.abs_addr));

    case MODE_IMPL:
      return snprintf(buffer, buflen, "%s", w->name);

    case MODE_IND:
      return snprintf(buffer, buflen, "%s  ($%04x)", w->name, e.arg16);
      return 0;

    case MODE_XIND:
      return snprintf(buffer, buflen, "%s  ($%04x,X)", w->name, e.arg16);

    case MODE_INDY:
      return snprintf(buffer, buflen, "%s  ($%04x),Y", w->name, e.arg16);

    case MODE_REL:
      return snprintf(buffer, buflen, "%s  %d ; ($%04x)", w->name, e.arg8, e.abs_addr);

    case MODE_ZEROP:
      return snprintf(buffer, buflen, "%s  $%02x", w->name, e.arg8);

    case MODE_ZEROPX:
      return snprintf(buffer, buflen, "%s  $%02x,X", w->name, e.arg8);

    case MODE_ZEROPY:
      return snprintf(buffer, buflen, "%s  $%02x,X", w->name, e.arg8);
  }

  // Delete this line when you're done
  return 0;
}


static void stk_push(mos6502_t* cpu, uint8_t value) {
  write8(cpu, 0x0100 + cpu->sp--, value);
}

static uint8_t stk_pop(mos6502_t* cpu) {
  return read8(cpu, 0x0100 + (++cpu->sp));
}

static int handle_irq(mos6502_t* cpu) {
  cpu->intr_status &= ~INTR_IRQ;

  // return if interrupts are disabled
  if (cpu->p.i == 0) return 0;


  // push the old PC
  stk_push(cpu, (cpu->pc >> 8) & 0xFF);
  stk_push(cpu, (cpu->pc) & 0xFF);

  // push the status with some bits changed
  cpu->p.i = 1;
  cpu->p.u = 1;
  cpu->p.b = 0;
  stk_push(cpu, cpu->p.val);

  // read new PC from fixed addr
  cpu->pc = read16(cpu, 0xFFFE);

  // and return the number of interrupts to do
  return 7;
}

static int handle_nmi(mos6502_t* cpu) {
  cpu->intr_status &= ~INTR_NMI;

  // push the old PC
  stk_push(cpu, (cpu->pc >> 8) & 0xFF);
  stk_push(cpu, (cpu->pc) & 0xFF);

  // push the status with some bits changed
  cpu->p.i = 1;
  cpu->p.u = 1;
  cpu->p.b = 0;
  stk_push(cpu, cpu->p.val);

  // read new PC from fixed addr
  cpu->pc = read16(cpu, 0xFFFA);

  // and return the number of interrupts to do
  return 8;
}

mos6502_step_result_t mos6502_step(mos6502_t* cpu) {



  int addnl_cycles = 0;
  if (cpu->intr_status & INTR_NMI) {
    addnl_cycles += handle_nmi(cpu);
  }

  if (cpu->intr_status & INTR_IRQ) {
    addnl_cycles += handle_irq(cpu);
  }


  enc_t enc;
  int newpc = decode(cpu, cpu->pc, &enc);
  if (enc.valid != 1) {
    fprintf(stderr, "%04x: INVALID INSTRUCTION (opcode=%02x)\n", cpu->pc,
            enc.opcode);
    return MOS6502_STEP_RESULT_ILLEGAL_INSTRUCTION;
  }

  char buf[50];


  mos6502_instr_repr(cpu, cpu->pc, buf, 50);
  // fprintf(stderr, "%02x %04x [%02x]: %s\n", cpu->intr_status, cpu->pc, enc.opcode, buf);

  // evaluate
  cpu->pc = newpc;
  widgets[enc.opcode].evaluator(cpu, &enc);

  mos6502_advance_clk(cpu, instr_cycles[enc.opcode] + enc.more_clk + addnl_cycles);
  return MOS6502_STEP_RESULT_SUCCESS;
}

#define NOT_IMPLEMENTED(name)                    \
  {                                              \
    fprintf(stderr, #name " NOT IMPLEMENTED\n"); \
    while (1)                                    \
      ;                                          \
  }
#define defop(name) void eval_##name(mos6502_t* cpu, enc_t* enc)

#define UINT8(X) ((X)&0xFFu)
#define CPU_SET_FLAG_ZERO(_CPU_, _VAL_) (_CPU_)->p.z = (UINT8(_VAL_) == 0x00u)
#define CPU_SET_FLAG_NEGATIVE(_CPU_, _VAL_) \
  (_CPU_)->p.n = ((UINT8(_VAL_) & 0x80u) ? 1 : 0)

void op_transfer(struct mos6502* cpu, const uint8_t* src, uint8_t* dest) {
  const uint8_t val = *src;
  *dest = val;
  CPU_SET_FLAG_ZERO(cpu, val);
  CPU_SET_FLAG_NEGATIVE(cpu, val);
}

void op_branch_cond(struct mos6502* cpu, enc_t *enc, const uint16_t addr, bool flag) {

  // branching can add additional cycles, which we need to handle!
  if (flag) {
    enc->more_clk++;


    // more cycles if branching over a page
    if ((addr & 0xFF00) != (cpu->pc & 0xFF00)) {
      enc->more_clk++;
    }
    cpu->pc = addr;
  }
}

// add with carry (not that you can without)
defop(ADC) {
  uint16_t operand = read8(cpu, enc->abs_addr);
  uint16_t t = (uint16_t)cpu->a + (uint16_t)operand + (uint16_t)cpu->p.c;

  // fix up the flags
  cpu->p.c = (t & 0xFF00) != 0;
  cpu->p.v = ((cpu->a ^ t) & (operand ^ t) & 0x80) != 0;

  cpu->a = t & 0x00FFu;
  CPU_SET_FLAG_ZERO(cpu, t);
  CPU_SET_FLAG_NEGATIVE(cpu, t);
}

defop(AND) {
  const uint8_t a = cpu->a;
  const uint8_t m = read8(cpu, enc->abs_addr);
  const uint8_t val = a & m;
  cpu->a = val;

  CPU_SET_FLAG_ZERO(cpu, val);
  CPU_SET_FLAG_NEGATIVE(cpu, val);
}

defop(ASL) {
  uint8_t val;
  bool carry = false;

  if (enc->mode == MODE_ACC) {
    val = cpu->a;
  } else {
    val = read8(cpu, enc->abs_addr);
  }

  carry = (val & 0x80u) != 0u;
  val <<= 1u;

  if (enc->mode == MODE_ACC) {
    cpu->a = val;
  } else {
    write8(cpu, enc->abs_addr, val);
  }

  CPU_SET_FLAG_ZERO(cpu, val);
  CPU_SET_FLAG_NEGATIVE(cpu, val);
  cpu->p.c = carry;
}

defop(BCC) { op_branch_cond(cpu, enc, enc->abs_addr, !cpu->p.c); }

defop(BCS) { op_branch_cond(cpu, enc, enc->abs_addr, cpu->p.c); }

defop(BEQ) {
  op_branch_cond(cpu, enc, enc->abs_addr, cpu->p.z);
}

defop(BIT) {
  uint16_t val = read8(cpu, enc->abs_addr);
  uint16_t t = cpu->a & val;
  cpu->p.z = (t & 0xFF) == 0;
  cpu->p.n = (val & 0x80);
  cpu->p.v = (val & (1 << 6));
}

defop(BMI) { op_branch_cond(cpu, enc, enc->abs_addr, cpu->p.n); }

defop(BNE) { op_branch_cond(cpu, enc, enc->abs_addr, !cpu->p.z); }

defop(BPL) { op_branch_cond(cpu, enc, enc->abs_addr, !cpu->p.n); }


defop(BRK) {
  // set the flags up correctly
  cpu->p.i = 1;
  cpu->p.b = 1;

  // set the PC to the one after the break
  cpu->pc++;
  // push the new PC
  stk_push(cpu, (cpu->pc >> 8) & 0xFF);
  stk_push(cpu, (cpu->pc) & 0xFF);

  // push the old status
  stk_push(cpu, cpu->p.val);
  cpu->p.b = 0;
  cpu->pc = read16(cpu, 0xFFFE);
}

defop(BVC) { op_branch_cond(cpu, enc, enc->abs_addr, !cpu->p.v); }

defop(BVS) { op_branch_cond(cpu, enc, enc->abs_addr, cpu->p.v); }

defop(CLC) { cpu->p.c = false; }

defop(CLD) { cpu->p.d = false; }

defop(CLI) { cpu->p.i = false; }

defop(CLV) { cpu->p.v = false; }

defop(CMP) {
  uint8_t M = read8(cpu, enc->abs_addr);

  uint16_t tmp = (uint16_t)cpu->a - (uint16_t)M;

  cpu->p.c = cpu->a >= M;
  cpu->p.z = tmp == 0;
  cpu->p.n = (tmp & 0x0080) != 0;
}
defop(CPX) {
  uint8_t M = read8(cpu, enc->abs_addr);
  uint16_t tmp = (uint16_t)cpu->x - (uint16_t)M;
  cpu->p.c = cpu->x >= M;
  cpu->p.z = tmp == 0;
  cpu->p.n = (tmp & 0x0080) != 0;
}
defop(CPY) {
  uint8_t M = read8(cpu, enc->abs_addr);
  uint16_t tmp = (uint16_t)cpu->y - (uint16_t)M;
  cpu->p.c = cpu->y >= M;
  cpu->p.z = tmp == 0;
  cpu->p.n = (tmp & 0x0080) != 0;
}

defop(DEC) {
  const uint16_t addr = enc->abs_addr;

  uint16_t val = read8(cpu, addr);
  val--;
  write8(cpu, addr, val);

  CPU_SET_FLAG_ZERO(cpu, val);
  CPU_SET_FLAG_NEGATIVE(cpu, val);
}

defop(DEX) {
  cpu->x--;
  CPU_SET_FLAG_ZERO(cpu, cpu->x);
  CPU_SET_FLAG_NEGATIVE(cpu, cpu->x);
}

defop(DEY) {
  cpu->y--;
  CPU_SET_FLAG_ZERO(cpu, cpu->y);
  CPU_SET_FLAG_NEGATIVE(cpu, cpu->y);
}


// exclusive or
defop(EOR) {
  cpu->a = cpu->a ^ read8(cpu, enc->abs_addr);
  cpu->p.z = cpu->a == 0;
  cpu->p.n = (cpu->a & 0x80) != 0;
}

defop(INC) {
  const uint16_t addr = enc->abs_addr;

  uint16_t val = read8(cpu, addr);
  val++;
  write8(cpu, addr, val);

  CPU_SET_FLAG_ZERO(cpu, val);
  CPU_SET_FLAG_NEGATIVE(cpu, val);
}

defop(INX) {
  cpu->x++;
  CPU_SET_FLAG_ZERO(cpu, cpu->x);
  CPU_SET_FLAG_NEGATIVE(cpu, cpu->x);
}

defop(INY) {
  cpu->y++;
  CPU_SET_FLAG_ZERO(cpu, cpu->y);
  CPU_SET_FLAG_NEGATIVE(cpu, cpu->y);
}

defop(JMP) {
  // simple enough
  cpu->pc = enc->abs_addr;
  // fprintf(stderr, "new addr = %04x\n", enc->abs_addr);
}

defop(JSR) {
  // this one is dumb, since it is actually relative to the old PC
  cpu->pc--;
  // push the upper half
  stk_push(cpu, (cpu->pc >> 8) & 0x00FF);
  // and the lower half
  stk_push(cpu, (cpu->pc & 0xFF));
  cpu->pc = enc->abs_addr;
}

defop(LDA) {
  cpu->a = read8(cpu, enc->abs_addr);
  cpu->p.z = cpu->a == 0x00;
  cpu->p.n = (cpu->a & 0x80u) != 0;
}

defop(LDX) {
  cpu->x = read8(cpu, enc->abs_addr);
  cpu->p.z = cpu->x == 0x00;
  cpu->p.n = cpu->x & 0x80u ? 1 : 0;
}

defop(LDY) {
  cpu->y = read8(cpu, enc->abs_addr);
  cpu->p.z = cpu->y == 0x00;
  cpu->p.n = cpu->y & 0x80u ? 1 : 0;
}

defop(LSR) {

  uint16_t val;
  if (enc->mode == MODE_ACC) {
    val = cpu->a;
  } else {
    val = read8(cpu, enc->abs_addr);
  }

  cpu->p.c = (val & 1);
  uint16_t temp = val >> 1;

  cpu->p.z = (temp & 0x00FF) == 0;
  cpu->p.n = (temp & 0x0080);
  if (enc->mode == MODE_IMPL || enc->mode == MODE_ACC) {
    cpu->a = temp & 0xFF;
  } else {
    write8(cpu, enc->abs_addr, temp & 0x00FF);
  }
}

defop(NOP) {}

defop(ORA) {
  uint8_t arg;
  if (enc->mode == MODE_IMM) {
    arg = enc->arg8;
  } else {
    arg = read8(cpu, enc->abs_addr);
  }

  uint8_t val = cpu->a;
  val |= arg;

  cpu->a = val;
  CPU_SET_FLAG_ZERO(cpu, val);
  CPU_SET_FLAG_NEGATIVE(cpu, val);
}
defop(PHA) {
  // push acculmulator to stack
  write8(cpu, 0x0100 + cpu->sp, cpu->a);
  cpu->sp--;
}

defop(PHP) {
  // push status register to stack
  // push acculmulator to stack
  write8(cpu, 0x0100 + cpu->sp, cpu->p.val);
  cpu->p.b = 0;
  cpu->p.z = 0;
  cpu->sp--;
}

defop(PLA) {
  cpu->sp++;
  cpu->a = read8(cpu, 0x0100 + cpu->sp);
  cpu->p.z = cpu->a == 0;
  cpu->p.n = cpu->a & 0x80;
}

defop(PLP) {
  cpu->sp++;
  cpu->p.val = read8(cpu, 0x0100 + cpu->sp);
  cpu->p.u = 1;
  cpu->p.b = 0;
}

defop(ROL) {
  uint16_t val;
  if (enc->mode == MODE_ACC) {
    val = cpu->a;
  } else {
    val = read8(cpu, enc->abs_addr);
  }
  uint16_t temp = (uint16_t)(val << 1) | cpu->p.c;

  cpu->p.c = (temp & 0xFF00) != 0;
  cpu->p.z = (temp & 0x00FF) == 0;
  cpu->p.n = (temp & 0x0080);
  if (enc->mode == MODE_IMPL || enc->mode == MODE_ACC) {
    cpu->a = temp & 0xFF;
  } else {
    write8(cpu, enc->abs_addr, temp & 0x00FF);
  }
}

defop(ROR) {
  uint16_t val;
  if (enc->mode == MODE_ACC) {
    val = cpu->a;
  } else {
    val = read8(cpu, enc->abs_addr);
  }

  uint16_t temp = ((uint16_t)cpu->p.c << 7) | (uint16_t)(val >> 1);

  // handle before rotation
  cpu->p.c = val & 1;
  cpu->p.z = (temp & 0x00FF) == 0;
  cpu->p.n = (temp & 0x0080);
  if (enc->mode == MODE_IMPL || enc->mode == MODE_ACC) {
    cpu->a = temp & 0xFF;
  } else {
    write8(cpu, enc->abs_addr, temp & 0x00FF);
  }
}

// return from interrupt
//
// pop the old status from the stack, and invert B and U
// then pop the new PC from the stack
defop(RTI) {
  cpu->p.val = stk_pop(cpu);
  cpu->p.b = 0;
  cpu->p.u = 1;

  cpu->pc = stk_pop(cpu) & 0xFF;
  cpu->pc |= ((uint16_t)stk_pop(cpu) << 8);
}


defop(RTS) {
  cpu->pc = stk_pop(cpu);
  cpu->pc |= stk_pop(cpu) << 8;
  cpu->pc++;
}

// subract with carry
// A = A - M - (1 - C)
// Flags changed: C, V, N, Z
defop(SBC) {
  uint16_t fetched = read8(cpu, enc->abs_addr);

  // printf("fetched = %04x\n", fetched);
  uint16_t value = ((uint16_t)fetched) ^ 0x00FF;

  uint16_t temp = (uint16_t)cpu->a + value + (uint16_t)cpu->p.c;
  cpu->p.c = (temp & 0xFF00) != 0;
  cpu->p.z = ((temp & 0x00FF) == 0);
  cpu->p.v = (temp ^ (uint16_t)cpu->a) & (temp ^ value) & 0x0080;
  cpu->p.n = (temp & 0x0080) != 0;
  cpu->a = temp & 0x00FF;
}

defop(SEC) { cpu->p.c = true; }

defop(SED) { cpu->p.d = true; }

defop(SEI) { cpu->p.i = true; }

defop(STA) { write8(cpu, enc->abs_addr, cpu->a); }

defop(STX) { write8(cpu, enc->abs_addr, cpu->x); }

defop(STY) { write8(cpu, enc->abs_addr, cpu->y); }

defop(TAX) { op_transfer(cpu, &cpu->a, &cpu->x); }

defop(TAY) { op_transfer(cpu, &cpu->a, &cpu->y); }

defop(TSX) { op_transfer(cpu, &cpu->sp, &cpu->x); }

defop(TXA) { op_transfer(cpu, &cpu->x, &cpu->a); }

defop(TXS) { op_transfer(cpu, &cpu->x, &cpu->sp); }

defop(TYA) { op_transfer(cpu, &cpu->y, &cpu->a); }

defop(VMCALL) {
  handle_vmcall(cpu, enc->arg8);
  NOT_IMPLEMENTED(VMCALL);
}

#define O(opname, opmode)                               \
  {                                                     \
    .valid = 1, .name = #opname, .mode = MODE_##opmode, \
    .evaluator = eval_##opname                          \
  }

// built from https://www.masswerk.at/6502/6502_instruction_set.html#RTI
static const widget_t widgets[256] = {

    // funky non-standard vmcall
    [0x80] = O(VMCALL, IMM),
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
    [0x96] = O(STX, ZEROPY),
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

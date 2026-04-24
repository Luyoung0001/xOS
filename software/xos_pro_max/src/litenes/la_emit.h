// la_emit.h - LoongArch32R 指令编码助手（简化版）
// 仅用于 JIT 动态生成机器码，不依赖汇编器。
#ifndef LA_EMIT_H
#define LA_EMIT_H

#include <stdint.h>

// R-type: opcode[31:26] | rj[25:21] | rd[20:16] | rk[15:11] | func[10:0]
#define LA_R(op, rj, rd, rk, func) ((uint32_t)(op) << 26 | (uint32_t)(rj) << 21 | (uint32_t)(rd) << 16 | (uint32_t)(rk) << 11 | (func))
// I-type: opcode[31:26] | rj[25:21] | rd[20:16] | imm[15:0]
#define LA_I(op, rj, rd, imm16)    ((uint32_t)(op) << 26 | (uint32_t)(rj) << 21 | (uint32_t)(rd) << 16 | ((uint16_t)(imm16)))

static inline uint32_t la_add_w(int rd, int rj, int rk) { return LA_R(0x00, rj, rd, rk, 0x20); } // add.w rd, rj, rk
static inline uint32_t la_sub_w(int rd, int rj, int rk) { return LA_R(0x00, rj, rd, rk, 0x22); } // sub.w
static inline uint32_t la_or(int rd, int rj, int rk)    { return LA_R(0x00, rj, rd, rk, 0x2A); }
static inline uint32_t la_and(int rd, int rj, int rk)   { return LA_R(0x00, rj, rd, rk, 0x28); }
static inline uint32_t la_xor(int rd, int rj, int rk)   { return LA_R(0x00, rj, rd, rk, 0x2C); }
static inline uint32_t la_sll_w(int rd, int rj, int sa) { return LA_R(0x00, rj, rd, sa, 0x04); } // sa in rk field
static inline uint32_t la_srl_w(int rd, int rj, int sa) { return LA_R(0x00, rj, rd, sa, 0x06); }
static inline uint32_t la_sra_w(int rd, int rj, int sa) { return LA_R(0x00, rj, rd, sa, 0x07); }

static inline uint32_t la_ori(int rd, int rj, int imm12){ return 0x03800000 | ((imm12 & 0xFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_andi(int rd, int rj, int imm12){ return 0x03400000 | ((imm12 & 0xFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_xori(int rd, int rj, int imm12){ return 0x03C00000 | ((imm12 & 0xFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_addi_w(int rd, int rj, int imm12){ return 0x02800000 | ((imm12 & 0xFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_sltui(int rd, int rj, int imm12){ return 0x02400000 | ((imm12 & 0xFFF) << 10) | (rj << 5) | rd; }

// load/store
static inline uint32_t la_ld_bu(int rd, int rj, int offs12){ return 0x28800000 | ((offs12 & 0xFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_ld_w(int rd, int rj, int offs12) { return 0x2C000000 | ((offs12 & 0xFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_st_b(int rd, int rj, int offs12){ return 0x29800000 | ((offs12 & 0xFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_st_w(int rd, int rj, int offs12){ return 0x2D000000 | ((offs12 & 0xFFF) << 10) | (rj << 5) | rd; }

// jump/link
static inline uint32_t la_jirl(int rd, int rj, int imm16){ return 0x4C000000 | ((imm16 & 0xFFFF) << 10) | (rj << 5) | rd; }
static inline uint32_t la_bl(int imm26) { return 0x54000000 | ((imm26 & 0x03FFFFFF)); } // pc-relative
static inline uint32_t la_b(int imm26)  { return 0x50000000 | ((imm26 & 0x03FFFFFF)); }
static inline uint32_t la_beq(int rj, int rk, int imm16){ return 0x58000000 | ((imm16 & 0xFFFF) << 10) | (rk << 5) | rj; }
static inline uint32_t la_bne(int rj, int rk, int imm16){ return 0x5C000000 | ((imm16 & 0xFFFF) << 10) | (rk << 5) | rj; }

// load upper
static inline uint32_t la_lu12i_w(int rd, int imm20){ return 0x14000000 | ((imm20 & 0xFFFFF) << 5) | rd; }

// helper: load 32-bit immediate
static inline int emit_li32(uint32_t *buf, int pos, int rd, uint32_t imm){
    buf[pos++] = la_lu12i_w(rd, imm >> 12);
    buf[pos++] = la_ori(rd, rd, imm & 0xFFF);
    return pos;
}

#endif // LA_EMIT_H

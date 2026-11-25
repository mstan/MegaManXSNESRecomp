#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cpu_state.h"

enum MmxQolFeature {
  MMX_QOL_NO_DOUBLE_TAP = 1u << 0,
  MMX_QOL_SUB_TANK = 1u << 1,
  MMX_QOL_AUTO_CHARGE = 1u << 2,
  MMX_QOL_RAPID_FIRE = 1u << 3,
  MMX_QOL_DIALOGUE = 1u << 4,
  MMX_QOL_SKIP_INTRO = 1u << 5,
  MMX_QOL_ESCAPE = 1u << 6,
  MMX_QOL_WALL_JUMP = 1u << 7,
  MMX_QOL_FIRST_HADOUKEN = 1u << 8,
  MMX_QOL_HADOUKEN_IFRAMES = 1u << 9,
  MMX_QOL_HADOUKEN_SIGMA = 1u << 10,
};
void MmxQolReset(void);
void MmxQolEnable(unsigned feature);
bool MmxQolEnabled(void);
bool MmxQolFeatureEnabled(unsigned features);
bool MmxQolRegisterHooks(void);
bool MmxQolDamageEnabled(void);
unsigned MmxQolDamage(const uint8_t *ram,unsigned enemy,unsigned table,
                      unsigned original,bool native_hadouken);
void MmxQolHadoukenOpcode(CpuState *cpu,uint8_t *ram,uint32_t pc,bool native_hadouken);
/* Input is already mapped by the cartridge. Never change recorded/raw pads. */
void MmxQolFireIntent(const uint8_t *ram, bool charge_ready, bool *held, bool *pressed);
unsigned MmxQolShotValue(const uint8_t *ram,unsigned original,bool held_read,bool charge_ready);
unsigned MmxQolUpgradeRead(uint8_t *ram,unsigned post_pc,unsigned original,bool is_x);
/* The normal generated-code pass and interpreter adapter share these owners. */
uint32_t MmxQolApplyOpcode(CpuState *cpu,uint32_t pc);
unsigned MmxQolNativeShotRead(CpuState *cpu,unsigned original,bool held_read);
unsigned MmxQolNativeUpgrade(CpuState *cpu,unsigned load_pc,unsigned original);
unsigned MmxQolNativeDamage(CpuState *cpu,unsigned original);
void MmxQolNativePlayer(CpuState *cpu,unsigned phase);
/* Called on the simulation thread immediately before the indicated opcode.
 * Zero means run the original instruction; otherwise resume at the returned PC.
 * The caller supplies the projected character/weapon owner, including co-op. */
uint32_t MmxQolOpcode(CpuState *cpu, uint8_t *ram, uint32_t pc,
                      bool is_x, bool native_buster, bool charge_ready);

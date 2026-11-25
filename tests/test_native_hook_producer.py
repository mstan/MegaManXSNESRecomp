"""Bounded compiled controls for the real producer, registry and title helper."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index('bool ' + name + '(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeHookProducer(unittest.TestCase):
    def test_both_regions_register_common_yields_and_graphics_checkpoint(self):
        sdk=Path(os.environ.get('SNESRECOMP_ROOT',ROOT/'snesrecomp'))
        cc=os.environ.get('CC')or shutil.which('cc')or shutil.which('gcc')
        if not cc or not(sdk/'runner/src/cpu_state.h').exists():self.skipTest('C compiler and SDK required')
        rtl=(ROOT/'src/mmx_rtl.c').read_text()
        def extract(signature):
            start=rtl.index(signature);end=rtl.index('{',start)+1;depth=1
            while depth:depth+=(rtl[end]=='{')-(rtl[end]=='}');end+=1
            return rtl[start:end]+'\n'
        source='''#include "cpu_state.h"
#include "common_rtl.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/interp_bridge.h"
#include "apu_frame_clock.h"
static Snes console;static Ppu ppu;Snes *g_snes=&console;Ppu *g_ppu=&ppu;
static uint64_t s_graphics_frame_start;static bool s_graphics_frame_active;
static unsigned count;static uint32_t pcs[4];
static void MmxNativeYieldHook(CpuState *c,uint32_t pc) { (void)c;(void)pc; }
static void MmxNativeTaskDieHook(CpuState *c,uint32_t pc) { (void)c;(void)pc; }
void interp_bridge_set_pre_opcode_hook(uint32_t pc,InterpPreOpcodeHook hook) {
  if(count>=4 || hook!=(count<3?MmxNativeYieldHook:MmxNativeTaskDieHook)) abort();
  pcs[count++]=pc;
}
'''
        source+=extract('bool MmxGraphicsShouldYield(')+extract('static void MmxRegisterNativeSchedulerHooks(')
        source+=r'''
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1; } } while(0)
int main(void) {
  MmxRegisterNativeSchedulerHooks();CHECK(count==4 && pcs[0]==0x8100 && pcs[1]==0x810c && pcs[2]==0x8121 && pcs[3]==0x80f8);
  CpuState c={0};s_graphics_frame_start=10;s_graphics_frame_active=true;
  console.nmiEnabled=true;ppu.inidisp=15;
  c.master_cycles=10+RTL_MASTER_CYCLES_PER_FRAME-0x18000-1;CHECK(!MmxGraphicsShouldYield(&c));
  c.master_cycles++;CHECK(MmxGraphicsShouldYield(&c));
  ppu.inidisp=0x80;CHECK(!MmxGraphicsShouldYield(&c));ppu.inidisp=15;
  console.nmiEnabled=false;CHECK(!MmxGraphicsShouldYield(&c));console.nmiEnabled=true;
  s_graphics_frame_active=false;CHECK(!MmxGraphicsShouldYield(&c));s_graphics_frame_active=true;
  c.master_cycles=9;CHECK(!MmxGraphicsShouldYield(&c));return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='mmx-region-yield-')as directory:
            p=Path(directory);c=p/'test.c';c.write_text(source)
            for jp in (0,1):
                exe=p/('region'+str(jp)+('.exe'if os.name=='nt'else''))
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-DMMX_VARIANT_JP='+str(jp),'-I'+str(sdk/'runner/src'),str(c),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)

    def test_fresh_task_matches_retail_scheduler_entry(self):
        sdk=Path(os.environ.get('SNESRECOMP_ROOT',ROOT/'snesrecomp'))
        cc=os.environ.get('CC')or shutil.which('cc')or shutil.which('gcc')
        if not cc or not(sdk/'runner/src/cpu_state.h').exists():self.skipTest('C compiler and SDK required')
        rtl=(ROOT/'src/mmx_rtl.c').read_text()
        def extract(signature):
            start=rtl.index(signature);end=rtl.index('{',start)+1;depth=1
            while depth:depth+=(rtl[end]=='{')-(rtl[end]=='}');end+=1
            return rtl[start:end]+'\n'
        start=rtl.index('typedef struct MmxSlotCpuSave')
        source='#include "cpu_state.h"\n'+rtl[start:rtl.index('} MmxSlotCpuSave;',start)+len('} MmxSlotCpuSave;')]+'\n'
        source+=extract('static inline void mmx_save_cpu(')+extract('static void mmx_seed_task_cpu(')
        source+=r'''
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1; } } while(0)
int main(void) {
  CpuState c={0};c.A=0xdead;c.X=0xbeef;c.Y=0xabcd;c.P=0x4c;c._flag_V=1;c._flag_I=1;c._flag_D=1;
  c.DB=0x86;c.PB=0x80;c.D=0;
  for(unsigned n=0;n<7;++n) {
    MmxSlotCpuSave s;uint16_t stack=0x13f+n*0x40;mmx_seed_task_cpu(&s,&c,n*16,stack);
    CHECK(s.saved && s.S==stack && s.A==stack && s.X==n*16 && s.Y==0xcd);
    CHECK(s.m_flag==1 && s.x_flag==1 && (s.P&0x31)==0x31 && s._flag_C);
    CHECK(s._flag_N==!!(stack&0x80) && !s._flag_Z && !!(s.P&0x80)==s._flag_N);
    CHECK(s._flag_V && s._flag_I && s._flag_D && s.DB==0x86 && s.PB==0x80 && !s.D);
  }
  CHECK(c.A==0xdead && c.X==0xbeef && !c.m_flag && !c.x_flag);
  puts("fresh native task: all seven retail entry stacks, slot X, stack A, 8-bit widths and flags");return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='mmx-task-entry-')as directory:
            p=Path(directory);c=p/'test.c';exe=p/('test.exe'if os.name=='nt'else'test');c.write_text(source)
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-I'+str(sdk/'runner/src'),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_interpreted_tail_uses_existing_native_yield_bodies(self):
        sdk = Path(os.environ.get('SNESRECOMP_ROOT', ROOT / 'snesrecomp'))
        compiler = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
        if not compiler or not (sdk/'runner/src/cpu_state.h').exists():
            self.skipTest('C compiler and initialized SDK required')
        def extract(text, signature):
            start=text.index(signature); end=text.index('{',start)+1; depth=1
            while depth:
                depth+=(text[end]=='{')-(text[end]=='}'); end+=1
            return text[start:end]
        rtl=(ROOT/'src/mmx_rtl.c').read_text()
        stubs=(ROOT/'src/gen_stubs.c').read_text()
        source='#include <stdlib.h>\n#include <setjmp.h>\n#include "cpu_state.h"\n#include "execution_mode.h"\n'
        source+=r'''
uint8_t g_ram[0x20000],g_mmx_task_slot_x; int snes_frame_counter;
static int policy=SNESRECOMP_EXECUTION_MODE_ON, yields, countdown, graphics, reference, returned;
static uint32_t destination;
CpuState g_cpu;
#define MMX_NSLOTS 7
static uint8_t g_current_slot_idx,g_slot_done[MMX_NSLOTS];
static void *g_scheduler_fiber=(void*)1;
static jmp_buf finished;static int host_contexts,switches;
static void SwitchToFiber(void *scheduler) {
  if(scheduler!=g_scheduler_fiber) abort();
  switches++;longjmp(finished,1);
}
void interp_bridge_with_host_context(CpuState *c,void(*host)(void*),void *arg) {
  if(c!=&g_cpu || !g_slot_done[g_current_slot_idx] || g_ram[0x30+g_mmx_task_slot_x]) abort();
  host_contexts++;host(arg);
}
SnesrecompExecutionMode snesrecomp_execution_mode(SnesrecompExecutionMode default_mode) { (void)default_mode;return policy; }
int interp_bridge_in_lle_scheduler(void) { return 0; }
static RecompReturn mmx_lle_unwind_to(CpuState *c,uint16_t p) { (void)c;(void)p;abort(); }
static int mmx_hle_diag_enabled(void) { return 0; }
static void RecompStackDump(void) {}
static void mmx_host_yield(uint8_t n) { yields++;countdown=n; }
static bool MmxGraphicsShouldYield(const CpuState *c) { (void)c;return graphics; }
static void MmxGraphicsYieldHook(CpuState *c,uint32_t p) { (void)c;(void)p;reference++; }
static void interp_bridge_pre_opcode_return(uint32_t p) { destination=p;returned++; }
'''
        for name in ('HleMmxYieldOneFrame','HleMmxYieldNFrames','HleMmxYieldVblank','HleMmxTaskDie'):
            source+=extract(stubs,'RecompReturn '+name+'(')+'\n'
        source+=extract(rtl,'static void MmxNativeYieldHook(')
        source+='static void mmx_finish_current_task(void);\n'
        source+=extract(rtl,'static void MmxNativeTaskDieHook(')
        source+=extract(rtl,'static void mmx_switch_to_scheduler(')
        source+=extract(rtl,'static void mmx_finish_current_task(void) {')
        source+=r'''
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1; } } while(0)
static void frame(CpuState *c) { c->S=0x13d;c->PB=0x80;g_ram[0x13e]=0x34;g_ram[0x13f]=0x92; }
int main(void) {
  CpuState c={0};frame(&c);MmxNativeYieldHook(&c,0x808100);
  CHECK(yields==1 && countdown==1 && c.S==0x13f && destination==0x809235 && returned==1);
  frame(&c);c.A=7;MmxNativeYieldHook(&c,0x810c);
  CHECK(yields==2 && countdown==7 && c.S==0x13f && returned==2);
  frame(&c);c.A=0;MmxNativeYieldHook(&c,0x810c);CHECK(yields==3 && countdown==1);
  frame(&c);MmxNativeYieldHook(&c,0x8121);CHECK(yields==3 && c.S==0x13f);
  frame(&c);graphics=1;MmxNativeYieldHook(&c,0x8121);CHECK(yields==4 && c.S==0x13f);
  frame(&c);graphics=0;g_ram[0xb9d]=0x80;MmxNativeYieldHook(&c,0x8121);CHECK(yields==5);
  policy=SNESRECOMP_EXECUTION_MODE_LLE;frame(&c);MmxNativeYieldHook(&c,0x8121);
  CHECK(reference==1 && yields==5 && c.S==0x13d);
  g_current_slot_idx=3;g_mmx_task_slot_x=0x30;g_ram[0x60]=3;g_cpu.S=0x13d;
  MmxNativeTaskDieHook(&g_cpu,0x80f8);CHECK(!host_contexts && !switches && g_ram[0x60]==3);
  policy=SNESRECOMP_EXECUTION_MODE_ON;
  if(!setjmp(finished)) { MmxNativeTaskDieHook(&g_cpu,0x80f8);abort(); }
  CHECK(host_contexts==1 && switches==1 && g_slot_done[3] && !g_ram[0x60]);
  CHECK(g_cpu.S==0x13d && !g_slot_done[0] && !g_slot_done[6]);
  puts("native yield tail: existing HLE bodies, inherited frame, countdown, conditional yield and explicit floor preserved");
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='mmx-native-yield-') as directory:
            path=Path(directory);c=path/'test.c';exe=path/('test.exe'if os.name=='nt'else'test')
            c.write_text(source)
            subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-I'+str(sdk/'runner/src'),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_unarmed_boundary_and_explicit_zero_redirect(self):
        sdk = Path(os.environ.get('SNESRECOMP_ROOT', ROOT / 'snesrecomp'))
        compiler = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
        if not compiler or not (sdk / 'runner/src/cpu_state.h').exists():
            self.skipTest('C compiler and initialized SDK required')
        spec = importlib.util.spec_from_file_location('native_hooks', ROOT / 'tools/apply_native_hooks.py')
        producer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(producer)
        body = '''
RecompReturn Probe(CpuState *cpu) {
  uint16_t _entry_s=cpu->S; uint8_t _hrv=2;
  L_8000_M1X1:
    cpu_trace_block(cpu, 0x008000);
    if (interp_bridge_lle_master_deadline_reached(cpu)) return RECOMP_RETURN_NORMAL;
    cpu->coprocessor_master_cycles = cpu->master_cycles;
    cpu->A++;
    return RECOMP_RETURN_NORMAL;
}
'''
        patched, count = producer.apply(body)
        self.assertEqual(count, 1)
        self.assertEqual(producer.apply(patched), (patched, count))
        self.assertLess(patched.index('interp_bridge_native_hooks_armed'), patched.index('MmxNativeGuestHook(cpu,'))
        bridge = (sdk / 'runner/src/snes/interp_bridge.c').read_text()
        registry = bridge[bridge.index('enum { kInterpPreOpcodeHookSlots'):bridge.index('/* BRK bridge seam.')]
        helper = function((ROOT / 'src/mmx_rtl.c').read_text(), 'MmxNativeGuestHook')
        helper = helper.replace('bool MmxNativeGuestHook(', 'static bool actual_guest_hook(')
        source = '#include <string.h>\n#include "cpu_state.h"\n#include "snes/interp_bridge.h"\n'
        source += registry
        source += 'static void legacy_zero(CpuState *, uint32_t);\nInterpPreOpcodeHook MmxZeroReferenceHook(void) { return legacy_zero; }\n'
        source += helper
        source += r'''
static unsigned guest_calls,tail_calls,effects,legacy_calls; static uint32_t destination;
bool MmxNativeGuestHook(CpuState *c,uint32_t p,unsigned mx,bool owned,uint32_t *target) {
  guest_calls++; return actual_guest_hook(c,p,mx,owned,target);
}
RecompReturn MmxNativeHookTail(CpuState *c,uint32_t p,uint16_t s,uint8_t h) {
  (void)c;(void)s;(void)h;tail_calls++;destination=p;return RECOMP_RETURN_SKIP_1;
}
#define cpu_trace_block(c,p) ((void)0)
#define RecompStackPop() ((void)0)
#define interp_bridge_lle_master_deadline_reached(c) false
static void zero(CpuState *c,uint32_t p) { (void)c;(void)p;interp_bridge_pre_opcode_redirect(0); }
static void mutate(CpuState *c,uint32_t p) { (void)p;c->Y++; }
static void legacy_zero(CpuState *c,uint32_t p) { (void)c;(void)p;effects++;legacy_calls++; }
void MmxRendererRecordPiece(const uint8_t *r,uint16_t d) { (void)r;(void)d;effects++; }
uint8_t g_ram[0x20000];
'''
        source += patched
        owned_body = body.replace('Probe(', 'OwnedProbe(').replace('L_8000_M1X1:', 'L_D76A_M1X0:').replace('0x008000', '0x00D76A')
        owned_body = owned_body.replace('    cpu->A++;', '    /*MMX-RENDER-PIECE*/ { extern uint8_t g_ram[0x20000]; extern void MmxRendererRecordPiece(const uint8_t *, uint16_t); MmxRendererRecordPiece(g_ram, cpu->D); }\n    cpu->cycles += 2;\n    cpu->A++;')
        owned, _ = producer.apply(owned_body)
        self.assertIn('2u, true, &target', owned)
        self.assertLess(owned.index('MmxRendererRecordPiece(g_ram'), owned.index('MmxNativeGuestHook(cpu,'))
        source += owned
        source += r'''
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
  CpuState c={0}; c.S=0x100;
  CHECK(Probe(&c)==RECOMP_RETURN_NORMAL && c.A==1 && guest_calls==0 && tail_calls==0);
  CHECK(interp_bridge_add_pre_opcode_hook(0x018000,mutate));
  CHECK(Probe(&c)==RECOMP_RETURN_NORMAL && guest_calls==1 && c.Y==0);
  interp_bridge_set_pre_opcode_hook(0,NULL);
  c.m_flag=c.x_flag=1;
  CHECK(interp_bridge_add_pre_opcode_hook(0x008000,zero));
  unsigned before=c.A;
  CHECK(Probe(&c)==RECOMP_RETURN_SKIP_1 && c.A==before && tail_calls==1 && destination==0);
  interp_bridge_set_pre_opcode_hook(0,NULL); c.x_flag=0;
  CHECK(interp_bridge_add_pre_opcode_hook(0x00d76a,legacy_zero));
  CHECK(interp_bridge_add_pre_opcode_hook(0x00d76a,mutate));
  CHECK(OwnedProbe(&c)==RECOMP_RETURN_NORMAL && effects==1 && legacy_calls==0 && c.Y==1);
  interp_bridge_set_pre_opcode_hook(0,NULL); c.x_flag=1;
  CHECK(interp_bridge_add_pre_opcode_hook(0x008000,legacy_zero));
  CHECK(Probe(&c)==RECOMP_RETURN_NORMAL && effects==2 && legacy_calls==1);
  puts("native producer: unarmed calls removed, full-key collision safe, zero redirect preserved");
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='mmx-native-producer-') as directory:
            path = Path(directory)
            c, exe = path / 'test.c', path / ('test.exe' if os.name == 'nt' else 'test')
            c.write_text(source)
            subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra',
                            '-I' + str(sdk / 'runner/src'), str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_renderer_capture_requires_every_actual_native_mode(self):
        spec = importlib.util.spec_from_file_location('capture', ROOT / 'tools/apply_renderer_capture.py')
        capture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(capture)
        capture.validate_modes({(1, 0)}, {(1, 0)}, 1)
        with self.assertRaises(ValueError):
            capture.validate_modes({(1, 0), (0, 0)}, {(1, 0)}, 1)
        with self.assertRaises(ValueError):
            capture.validate_modes({(1, 0)}, set(), 0)


if __name__ == '__main__':
    unittest.main()

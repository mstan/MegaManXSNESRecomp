#include "mmx_boss_rush_audio.h"
#include "mmx_boss_rush.h"
#include "mod_audio.h"
#include "common_rtl.h"
#include "snes/apu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Commands observed in all 28 native boss pairs, including owned projectiles.
 * $0C is the repeated meter fill. Charge/stop, music and upload commands remain
 * native. $B1 wraps to the driver's null effect: it acknowledges normally and
 * leaves active voices alone ($048A..04B5, common SPC bank 0). */
static const uint8_t commands[]={
  0x0e,0x11,0x13,0x1e,0x23,0x31,0x33,0x44,0x48,0x49,0x4a,0x4c,
  0x4d,0x4f,0x50,0x51,0x52,0x53,0x56,0x58,0x59,0x5a,0x5b,0x72,
  0x76,0x78,0x79,0x7a,0x7b,0x7c,0xa1
};
enum { RATE=32040, MAX_FRAMES=RATE*5, VOICES=8 };
typedef struct Clip { int16_t *pcm;uint32_t frames; } Clip;
typedef struct Playback {
  SNESModAudioClip clip;
  SNESModAudioVoice voice;
  uint32_t generation;
  uint16_t object;
  uint8_t seat,command,kind;
} Playback;
static Clip clips[256];
static Playback voices[VOICES];
static MmxBossRushAudioState state;
static const uint8_t *source_rom;
static uint32_t host_generation;
static int delivered_frame=-1;
static unsigned replace_voice;
extern uint8_t g_ram[0x20000];

static bool supported(unsigned command) {
  for(unsigned i=0;i<sizeof(commands);++i) if(commands[i]==command) return true;
  return false;
}
static bool emitter_valid(unsigned object) {
  if(object>=0xe68 && object<0x1228) return (object-0xe68)%64==0;
  if(object>=0x1428 && object<0x1628) return (object-0x1428)%64==0;
  if(object>=0x1628 && object<0x1928) return (object-0x1628)%48==0;
  return object>=0x1928 && object<0x1d08 && (object-0x1928)%32==0;
}
MmxBossRushAudioState MmxBossRushAudioGetState(void) {return state;}
bool MmxBossRushAudioValidState(const MmxBossRushAudioState *s) {
  if(!s) return false;
  for(unsigned i=0;i<MMX_RUSH_AUDIO_CUES;++i)
    if(s->cues[i].seat>=2 || s->cues[i].reserved ||
        (s->cues[i].command && (!supported(s->cues[i].command) ||
         !emitter_valid(s->cues[i].object)))) return false;
  return true;
}
bool MmxBossRushAudioSetState(const MmxBossRushAudioState *s) {
  if(!MmxBossRushAudioValidState(s)) return false;
  state=*s;return true;
}
void MmxBossRushAudioReset(void) {memset(&state,0,sizeof(state));}
void MmxBossRushAudioTick(void) {++state.frame;}
void MmxBossRushAudioLoaded(void) {
  /* A successful load already stops host voices. Do not replay historical
   * events carried by the restored snapshot. Rollback uses its own delivery
   * contract; the caller skips this delivery reset in that context. */
  for(unsigned i=0;i<VOICES;++i) {
    if(voices[i].clip) snes_mod_audio_unregister(voices[i].clip);
    memset(voices+i,0,sizeof(voices[i]));
  }
  delivered_frame=snes_frame_counter-1;host_generation=snes_mod_audio_reset_generation();
}
static unsigned word(const uint8_t *p) {return p[0]|p[1]<<8;}
static Apu *base_apu(const uint8_t *rom,size_t size) {
  if(!rom || size<0x48000+0x48*3) return NULL;
  Apu *a=apu_init();if(!a) return NULL;apu_reset(a);unsigned entry=0;
  /* $89:8000 packs the driver, effect programs and BRR samples. Include the
   * battle bank's $42/$47 instrument overlays, but no music sequences. */
  static const uint8_t banks[]={0,1,2,0x42,0x47};
  for(unsigned i=0;i<sizeof(banks);++i) {
    unsigned bank=banks[i];
    const uint8_t *ptr=rom+0x48000+bank*3;
    size_t p=(size_t)(9+ptr[2])*0x8000+(word(ptr)&0x7fff);
    bool ended=false;
    for(unsigned block=0;block<64;++block) {
      if(p>size || size-p<4) break;
      unsigned n=word(rom+p),d=word(rom+p+2);p+=4;
      if(!n) {if(!bank) entry=d;ended=true;break;}
      if(n>65536u-d || p>size || n>size-p) break;
      memcpy(a->ram+d,rom+p,n);p+=n;
    }
    if(!ended) {apu_free(a);return NULL;}
  }
  if(entry!=0x2c0 || a->ram[0x1c00]!=0xb1) {apu_free(a);return NULL;}
  a->romReadable=false;a->spc->pc=(uint16_t)entry;
  for(unsigned i=0;i<32768;++i) apu_cycle(a);
  if(a->outPorts[2]!=1 || a->outPorts[3]!=0 || a->spc->stopped) {apu_free(a);return NULL;}
  a->dsp->sampleRead=a->dsp->sampleWrite=0;return a;
}
static Clip render(const Apu *base,unsigned command) {
  Clip result={0};Apu *a=apu_init();if(!a) return result;
  /* Preserve each instance's owned pointers/shadow mixer while cloning the
   * initialized native driver. Neither guest APU nor live DSP is touched. */
  memcpy(a->ram,base->ram,sizeof(*a)-offsetof(Apu,ram));
  *a->spc=*base->spc;a->spc->apu=a;
  memcpy(a->dsp->ram,base->dsp->ram,sizeof(Dsp)-offsetof(Dsp,ram));
  int16_t *pcm=malloc(MAX_FRAMES*2*sizeof(*pcm));
  if(!pcm) {apu_free(a);return result;}
  a->inPorts[0]=(uint8_t)command;a->inPorts[1]=0;
  unsigned quiet=0,last=0,samples=0;bool audible=false,complete=false;
  for(;samples<MAX_FRAMES;++samples) {
    for(unsigned i=0;i<32;++i) apu_cycle(a);
    if(dsp_getSamples(a->dsp,pcm+samples*2,1)!=1) break;
    if(pcm[samples*2] || pcm[samples*2+1]) {last=samples;quiet=0;audible=true;}
    else ++quiet;
    if(audible && !a->ram[0xc3] && quiet>=1024) {complete=true;break;}
  }
  /* $5A is Kuwanger's sustained boomerang sound. Its emitter leaves this
   * room during its round trip; stop the cached voice when that native object
   * retires rather than waiting for an unrelated SPC preemption. Retain up to
   * five seconds of native sustain if the script has not yet ended. */
  if(audible && (complete || command==0x5a) && a->outPorts[2]==2 && !a->spc->stopped) {
    result.frames=complete?last+65:samples;
    result.pcm=realloc(pcm,result.frames*2*sizeof(*pcm));
    if(!result.pcm) result.pcm=pcm;
  } else free(pcm);
  apu_free(a);return result;
}
void MmxBossRushAudioPrepare(const uint8_t *rom,size_t size) {
  if(source_rom==rom || !rom) return;
  MmxBossRushAudioLoaded();
  for(unsigned i=0;i<256;++i) {free(clips[i].pcm);clips[i]=(Clip){0};}
  source_rom=rom;Apu *base=base_apu(rom,size);
  if(!base) {fprintf(stderr,"[mmx-rush] native sound cache could not initialize\n");return;}
  unsigned cached=0;size_t bytes=0;
  for(unsigned i=0;i<sizeof(commands);++i) {
    unsigned command=commands[i];clips[command]=render(base,command);
    if(clips[command].pcm) {++cached;bytes+=clips[command].frames*2*sizeof(int16_t);}
    else fprintf(stderr,"[mmx-rush] native effect $%02X could not be cached\n",command);
  }
  apu_free(base);
  fprintf(stderr,"[mmx-rush] cached %u native boss effects (%zu bytes) from the supplied ROM\n",cached,bytes);
}
const int16_t *MmxBossRushAudioClip(unsigned command,uint32_t *frames) {
  if(command>=256) return NULL;
  if(frames) *frames=clips[command].frames;return clips[command].pcm;
}
bool MmxBossRushAudioQueue(unsigned seat,unsigned object,unsigned command,unsigned pan,uint32_t generation) {
  /* Keep the original request if a private cache was unavailable. Native child
   * pools use 64-, 48- and 32-byte objects, not one uniform stride. */
  if(seat>=2 || !supported(command) || !clips[command].pcm || !emitter_valid(object)) return false;
  state.cues[state.sequence++%MMX_RUSH_AUDIO_CUES]=(MmxBossRushAudioCue){
    .generation=generation,.frame=state.frame,
    .command=(uint8_t)command,.pan=(uint8_t)pan,
    .seat=(uint8_t)seat,.object=(uint16_t)object,.kind=g_ram[object+10]};
  return true;
}
static void retire(unsigned i) {
  if(voices[i].clip) snes_mod_audio_unregister(voices[i].clip);
  memset(voices+i,0,sizeof(voices[i]));
}
static void play(const MmxBossRushAudioCue *cue) {
  const Clip *clip=clips+cue->command;if(!clip->pcm) return;
  if(cue->command==0x5a) for(unsigned i=0;i<VOICES;++i)
    if(voices[i].command==0x5a && voices[i].object==cue->object) retire(i);
  unsigned slot=0;while(slot<VOICES && voices[slot].clip) ++slot;
  if(slot==VOICES) {slot=replace_voice++%VOICES;retire(slot);}
  /* Preserve the request's signed positional balance. Native effect scripting
   * and stereo character are already baked into the center-rendered clip. */
  int pan=(int8_t)cue->pan;
  int left=pan>0?128-pan:128,right=pan<0?128+pan:128;
  int16_t *pcm=malloc(clip->frames*2*sizeof(*pcm));if(!pcm) return;
  for(unsigned i=0;i<clip->frames;++i) {
    pcm[i*2]=(int16_t)(clip->pcm[i*2]*left/128);
    pcm[i*2+1]=(int16_t)(clip->pcm[i*2+1]*right/128);
  }
  SNESModAudioClip handle=snes_mod_audio_register_pcm_s16(pcm,clip->frames,RATE,2);free(pcm);
  if(!handle) return;
  SNESModAudioVoice voice=snes_mod_audio_play_voice(handle,100);
  if(!voice) {snes_mod_audio_unregister(handle);return;}
  voices[slot]=(Playback){.clip=handle,.voice=voice,.generation=cue->generation,
      .object=cue->object,.seat=cue->seat,.command=cue->command,.kind=cue->kind};
}
void MmxBossRushAudioPresent(void) {
  MmxBossRushState rush=MmxBossRushGetState();
  uint32_t generation=snes_mod_audio_reset_generation();
  if(generation!=host_generation) {MmxBossRushAudioLoaded();return;}
  for(unsigned i=0;i<VOICES;++i) if(voices[i].clip &&
      (!snes_mod_audio_voice_active(voices[i].voice) || rush.mode!=MMX_RUSH_PLAYING ||
       rush.bosses[voices[i].seat].generation!=voices[i].generation ||
       rush.bosses[voices[i].seat].phase==MMX_RUSH_DYING ||
       (voices[i].command==0x5a && (!g_ram[voices[i].object] ||
         g_ram[voices[i].object+10]!=voices[i].kind ||
         MmxBossRushOwner(voices[i].object)!=(int)voices[i].seat)))) retire(i);
  /* Resimulation may change the number of historical requests. Deliver only
   * the committed frame, rather than treating a rewound sequence as new cues. */
  int frame=snes_frame_counter-1;if(frame==delivered_frame) return;
  delivered_frame=frame;
  uint32_t count=state.sequence<MMX_RUSH_AUDIO_CUES?state.sequence:MMX_RUSH_AUDIO_CUES;
  for(uint32_t n=state.sequence-count;n!=state.sequence;++n) {
    const MmxBossRushAudioCue *cue=state.cues+n%MMX_RUSH_AUDIO_CUES;
    if(cue->frame==state.frame && rush.mode==MMX_RUSH_PLAYING && cue->generation==rush.bosses[cue->seat].generation &&
        rush.bosses[cue->seat].phase!=MMX_RUSH_DYING &&
        (cue->command!=0x5a || (g_ram[cue->object] && g_ram[cue->object+10]==cue->kind))) play(cue);
  }
}

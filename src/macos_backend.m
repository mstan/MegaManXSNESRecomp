#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>
#import <dispatch/dispatch.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_metal.h>

#include "macos_backend.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "config.h"
#include "host_report.h"
#include "mmx_rtl.h"
#include "mmx_display.h"
#include "util.h"

#define CRT_SYSTEM CRT_SYSTEM_SNES
#include "crt_core.h"
#ifdef __cplusplus
}
#endif

typedef struct MetalRenderer {
  SDL_MetalView view;
  CAMetalLayer *layer;
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLRenderPipelineState> pipeline;
  id<MTLSamplerState> sampler;
  id<MTLTexture> textures[3];
  uint8_t *pixels;
  uint8_t *retro_pixels;
  size_t pixels_size;
  size_t retro_pixels_size;
  int width, height;
  int texture_width, texture_height;
  unsigned texture_index;
  struct CRT crt;
  struct NTSC_SETTINGS ntsc;
  bool crt_initialized;
  bool retro_enabled;
  int retro_scanlines;
  int retro_blend;
  int retro_noise;
  int retro_hue;
  unsigned retro_frame;
  unsigned retro_field;
} MetalRenderer;

static MetalRenderer g_metal;

typedef struct MetalVertex {
  vector_float2 position;
  vector_float2 uv;
} MetalVertex;

static bool MacMetal_Init(SDL_Window *window) {
  memset(&g_metal, 0, sizeof(g_metal));
  memset(&g_metal.ntsc, 0, sizeof(g_metal.ntsc));
  g_metal.retro_scanlines = 1;
  g_metal.retro_blend = 1;
  g_metal.device = MTLCreateSystemDefaultDevice();
  if (!g_metal.device) {
    fprintf(stderr, "Metal device creation failed\n");
    return false;
  }
  g_metal.queue = [g_metal.device newCommandQueue];
  g_metal.view = SDL_Metal_CreateView(window);
  if (!g_metal.view) {
    fprintf(stderr, "SDL Metal view creation failed: %s\n", SDL_GetError());
    return false;
  }
  g_metal.layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(g_metal.view);
  g_metal.layer.device = g_metal.device;
  g_metal.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  g_metal.layer.framebufferOnly = YES;

  NSString *source = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct V { float2 position [[attribute(0)]]; float2 uv [[attribute(1)]]; };\n"
    "struct O { float4 position [[position]]; float2 uv; };\n"
    "vertex O vs(V v [[stage_in]]) { O o; o.position=float4(v.position,0,1); o.uv=v.uv; return o; }\n"
    "fragment float4 fs(O in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]]) { return tex.sample(s,in.uv); }\n";
  NSError *error = nil;
  id<MTLLibrary> library = [g_metal.device newLibraryWithSource:source options:nil error:&error];
  if (!library) {
    fprintf(stderr, "Metal shader compilation failed: %s\n", error.localizedDescription.UTF8String);
    return false;
  }
  MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
  descriptor.vertexFunction = [library newFunctionWithName:@"vs"];
  descriptor.fragmentFunction = [library newFunctionWithName:@"fs"];
  MTLVertexDescriptor *vertex = [MTLVertexDescriptor vertexDescriptor];
  vertex.attributes[0].format = MTLVertexFormatFloat2;
  vertex.attributes[0].offset = 0;
  vertex.attributes[0].bufferIndex = 0;
  vertex.attributes[1].format = MTLVertexFormatFloat2;
  vertex.attributes[1].offset = sizeof(vector_float2);
  vertex.attributes[1].bufferIndex = 0;
  vertex.layouts[0].stride = sizeof(MetalVertex);
  vertex.layouts[0].stepRate = 1;
  vertex.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
  descriptor.vertexDescriptor = vertex;
  descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  g_metal.pipeline = [g_metal.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (!g_metal.pipeline) {
    fprintf(stderr, "Metal pipeline creation failed: %s\n", error.localizedDescription.UTF8String);
    return false;
  }
  MTLSamplerDescriptor *sampler = [MTLSamplerDescriptor new];
  sampler.sAddressMode = MTLSamplerAddressModeClampToEdge;
  sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
  sampler.minFilter = g_config.linear_filtering ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  sampler.magFilter = sampler.minFilter;
  g_metal.sampler = [g_metal.device newSamplerStateWithDescriptor:sampler];
  MacUi_Init(window, (__bridge void *)g_metal.device);
  return true;
}

static void MacMetal_Destroy(void) {
  MacUi_Shutdown();
  free(g_metal.pixels);
  free(g_metal.retro_pixels);
  g_metal.pixels = NULL;
  g_metal.retro_pixels = NULL;
  g_metal.pixels_size = 0;
  g_metal.retro_pixels_size = 0;
  for (int i = 0; i < 3; ++i) g_metal.textures[i] = nil;
  if (g_metal.view) SDL_Metal_DestroyView(g_metal.view);
  memset(&g_metal, 0, sizeof(g_metal));
}

static void MacMetal_GetOutputSize(int *width, int *height) {
  if (!g_metal.layer) {
    *width = 0;
    *height = 0;
    return;
  }
  CGSize size = g_metal.layer.drawableSize;
  *width = (int)size.width;
  *height = (int)size.height;
}

static void MacMetal_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  size_t size = (size_t)width * (size_t)height * 4;
  if (size > g_metal.pixels_size) {
    uint8_t *new_pixels = (uint8_t *)realloc(g_metal.pixels, size);
    if (!new_pixels) Die("Metal framebuffer allocation failed");
    g_metal.pixels = new_pixels;
    g_metal.pixels_size = size;
  }
  size_t retro_size = size;
  if (retro_size > g_metal.retro_pixels_size || !g_metal.retro_pixels) {
    uint8_t *new_retro_pixels = (uint8_t *)realloc(g_metal.retro_pixels, retro_size);
    if (!new_retro_pixels) Die("CRT framebuffer allocation failed");
    g_metal.retro_pixels = new_retro_pixels;
    g_metal.retro_pixels_size = retro_size;
  }
  if (!g_metal.crt_initialized || g_metal.crt.outw != width || g_metal.crt.outh != height) {
    if (!g_metal.crt_initialized)
      crt_init(&g_metal.crt, width, height,
               CRT_PIX_FORMAT_BGRA, g_metal.retro_pixels);
    else
      crt_resize(&g_metal.crt, width, height,
                 CRT_PIX_FORMAT_BGRA, g_metal.retro_pixels);
    g_metal.crt_initialized = true;
    g_metal.crt.scanlines = g_metal.retro_scanlines;
    g_metal.crt.blend = g_metal.retro_blend;
  }
  g_metal.width = width;
  g_metal.height = height;
  *pixels = g_metal.pixels;
  *pitch = width * 4;
}

static void MacMetal_CreateTextures(int width, int height) {
  for (int i = 0; i < 3; ++i) g_metal.textures[i] = nil;
  MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                          width:width
                                                                                         height:height
                                                                                      mipmapped:NO];
  descriptor.usage = MTLTextureUsageShaderRead;
  descriptor.storageMode = MTLStorageModeShared;
  for (int i = 0; i < 3; ++i) g_metal.textures[i] = [g_metal.device newTextureWithDescriptor:descriptor];
}

static void MacMetal_EndDraw(void) {
  const uint8_t *frame_pixels = g_metal.pixels;
  int frame_width = g_metal.width;
  int frame_height = g_metal.height;
  if (g_metal.retro_enabled) {
    g_metal.ntsc.data = g_metal.pixels;
    g_metal.ntsc.format = CRT_PIX_FORMAT_BGRA;
    g_metal.ntsc.w = g_metal.width;
    g_metal.ntsc.h = g_metal.height;
    g_metal.ntsc.raw = 0;
    g_metal.ntsc.as_color = 1;
    g_metal.ntsc.field = g_metal.retro_field & 1;
    g_metal.ntsc.frame = g_metal.retro_frame & 1;
    g_metal.ntsc.hue = g_metal.retro_hue;
    g_metal.ntsc.dot_crawl_offset = g_metal.retro_frame % CRT_CC_VPER;
    crt_modulate(&g_metal.crt, &g_metal.ntsc);
    crt_demodulate(&g_metal.crt, g_metal.retro_noise);
    for (int y = 0; y < g_metal.height; ++y)
      for (int x = 0; x < g_metal.width; ++x)
        g_metal.retro_pixels[(y * g_metal.width + x) * 4 + 3] = 255;
    g_metal.retro_field ^= 1;
    if (g_metal.retro_field == 0) g_metal.retro_frame ^= 1;
    frame_pixels = g_metal.retro_pixels;
    frame_width = g_metal.width;
    frame_height = g_metal.height;
  }
  if (!g_metal.textures[0] || g_metal.texture_width != frame_width ||
      g_metal.texture_height != frame_height) {
    MacMetal_CreateTextures(frame_width, frame_height);
    g_metal.texture_width = frame_width;
    g_metal.texture_height = frame_height;
  }
  id<MTLTexture> texture = g_metal.textures[g_metal.texture_index++ % 3];
  MTLRegion region = MTLRegionMake2D(0, 0, frame_width, frame_height);
  [texture replaceRegion:region mipmapLevel:0 withBytes:frame_pixels bytesPerRow:frame_width * 4];

  id<CAMetalDrawable> drawable = [g_metal.layer nextDrawable];
  if (!drawable) return;
  MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = drawable.texture;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

  float viewport_w = (float)g_metal.layer.drawableSize.width;
  float viewport_h = (float)g_metal.layer.drawableSize.height;
  MmxDisplayViewport viewport;
  MmxDisplay_ComputeViewport(frame_width, frame_height,
                             (int)viewport_w, (int)viewport_h,
                             g_config.ignore_aspect_ratio, true,
                             &viewport);
  float left = (float)viewport.x;
  float top = (float)viewport.y;
  float right = left + (float)viewport.width;
  float bottom = top + (float)viewport.height;
  float ndc_left = left * 2.0f / viewport_w - 1.0f;
  float ndc_right = right * 2.0f / viewport_w - 1.0f;
  float ndc_top = 1.0f - top * 2.0f / viewport_h;
  float ndc_bottom = 1.0f - bottom * 2.0f / viewport_h;
  MetalVertex vertices[] = {
    {{ndc_left, ndc_top}, {0, 0}}, {{ndc_left, ndc_bottom}, {0, 1}},
    {{ndc_right, ndc_top}, {1, 0}}, {{ndc_right, ndc_bottom}, {1, 1}},
  };
  id<MTLCommandBuffer> command = [g_metal.queue commandBuffer];
  id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
  [encoder setRenderPipelineState:g_metal.pipeline];
  [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
  [encoder setFragmentTexture:texture atIndex:0];
  [encoder setFragmentSamplerState:g_metal.sampler atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
  MacUi_Render((__bridge void *)pass, (__bridge void *)command, (__bridge void *)encoder);
  [encoder endEncoding];
  [command presentDrawable:drawable];
  [command commit];
}

static const struct RendererFuncs kMacMetalRendererFuncs = {
  MacMetal_Init, MacMetal_Destroy, MacMetal_GetOutputSize,
  MacMetal_BeginDraw, MacMetal_EndDraw
};

void MacMetalRenderer_Create(struct RendererFuncs *funcs) {
  *funcs = kMacMetalRendererFuncs;
}

void MacMetal_SetRetroEnabled(bool enabled) { g_metal.retro_enabled = enabled; }
bool MacMetal_GetRetroEnabled(void) { return g_metal.retro_enabled; }

void MacMetal_SetRetroOptions(int scanlines, int blend, int noise, int hue) {
  g_metal.retro_scanlines = scanlines != 0;
  g_metal.retro_blend = blend != 0;
  g_metal.retro_noise = noise < 0 ? 0 : noise;
  g_metal.retro_hue = hue;
  g_metal.crt.scanlines = g_metal.retro_scanlines;
  g_metal.crt.blend = g_metal.retro_blend;
}

void MacMetal_GetRetroOptions(int *scanlines, int *blend, int *noise, int *hue) {
  if (scanlines) *scanlines = g_metal.retro_scanlines;
  if (blend) *blend = g_metal.retro_blend;
  if (noise) *noise = g_metal.retro_noise;
  if (hue) *hue = g_metal.retro_hue;
}

static AudioUnit g_audio_unit;
static float g_audio_volume = 1.0f;
static int16_t *g_audio_buffer;
static int g_audio_buffer_frames;
static int g_audio_buffer_cursor;

static double MacAudio_DefaultOutputRate(void) {
  AudioDeviceID device = kAudioObjectUnknown;
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL,
                                 &size, &device) != noErr ||
      device == kAudioObjectUnknown)
    return 48000.0;
  address.mSelector = kAudioDevicePropertyNominalSampleRate;
  Float64 rate = 48000.0;
  size = sizeof(rate);
  if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &rate) != noErr ||
      rate < 8000.0 || rate > 192000.0)
    return 48000.0;
  return rate;
}

static OSStatus MacAudio_Render(void *refcon, AudioUnitRenderActionFlags *flags,
                                const AudioTimeStamp *timestamp, UInt32 bus,
                                UInt32 frames, AudioBufferList *data) {
  (void)refcon; (void)flags; (void)timestamp; (void)bus;
  if (data->mNumberBuffers != 1 || !data->mBuffers[0].mData || !g_audio_buffer)
    return noErr;

  int16_t *output = (int16_t *)data->mBuffers[0].mData;
  static int callback_reported;
  if (!callback_reported) {
    callback_reported = 1;
    host_report_breadcrumb("native audio callback: frames=%u buffers=%u bytes=%u block=%d",
                           (unsigned)frames, (unsigned)data->mNumberBuffers,
                           (unsigned)data->mBuffers[0].mDataByteSize,
                           g_audio_buffer_frames);
  }
  UInt32 remaining = frames;
  while (remaining != 0) {
    if (g_audio_buffer_cursor == g_audio_buffer_frames) {
      /* RtlRenderAudio consumes exactly one 534-sample SNES DSP block and
       * resamples it into the host-rate block. Core Audio callbacks are not
       * required to be one video frame long, so retain the unconsumed tail
       * exactly like the former SDL callback did. */
      RtlRenderAudio(g_audio_buffer, g_audio_buffer_frames, 2);
      g_audio_buffer_cursor = 0;
    }
    int available = g_audio_buffer_frames - g_audio_buffer_cursor;
    int count = (int)remaining < available ? (int)remaining : available;
    for (int i = 0; i < count * 2; ++i) {
      int value = (int)lrintf(g_audio_buffer[(size_t)g_audio_buffer_cursor * 2 + i] * g_audio_volume);
      output[i] = (int16_t)(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
    }
    output += count * 2;
    g_audio_buffer_cursor += count;
    remaining -= (UInt32)count;
  }
  return noErr;
}

bool MacAudio_Init(int requested_frequency, int requested_samples,
                   int *actual_frequency, int *actual_channels,
                   int *frames_per_block) {
  AudioComponentDescription description = {
    kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
    kAudioUnitManufacturer_Apple, 0, 0
  };
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (!component || AudioComponentInstanceNew(component, &g_audio_unit) != noErr) return false;
  double output_rate = MacAudio_DefaultOutputRate();
  AudioStreamBasicDescription format = {
    .mSampleRate = output_rate, .mFormatID = kAudioFormatLinearPCM,
    .mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
    .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4,
    .mChannelsPerFrame = 2, .mBitsPerChannel = 16
  };
  if (AudioUnitSetProperty(g_audio_unit, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr)
    return false;
  AURenderCallbackStruct callback = { MacAudio_Render, NULL };
  if (AudioUnitSetProperty(g_audio_unit, kAudioUnitProperty_SetRenderCallback,
                           kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr)
    return false;
  if (AudioUnitInitialize(g_audio_unit) != noErr) return false;
  AudioStreamBasicDescription negotiated = {0};
  UInt32 negotiated_size = sizeof(negotiated);
  AudioUnitGetProperty(g_audio_unit, kAudioUnitProperty_StreamFormat,
                       kAudioUnitScope_Input, 0, &negotiated, &negotiated_size);
  double client_rate = negotiated.mSampleRate > 0.0 ? negotiated.mSampleRate : output_rate;
  int client_channels = negotiated.mChannelsPerFrame == 2 ? 2 : 0;
  if (!client_channels) {
    AudioUnitUninitialize(g_audio_unit);
    AudioComponentInstanceDispose(g_audio_unit);
    g_audio_unit = NULL;
    return false;
  }
  *actual_frequency = (int)lrint(client_rate);
  *actual_channels = client_channels;
  *frames_per_block = (534 * *actual_frequency + 32040 / 2) / 32040;
  g_audio_buffer_frames = *frames_per_block;
  g_audio_buffer = (int16_t *)calloc((size_t)g_audio_buffer_frames * 2,
                                     sizeof(*g_audio_buffer));
  if (!g_audio_buffer) return false;
  g_audio_buffer_cursor = g_audio_buffer_frames;
  host_report_breadcrumb("native audio format: device=%.2f negotiated=%.2f channels=%u block=%d",
                         output_rate, negotiated.mSampleRate,
                         (unsigned)negotiated.mChannelsPerFrame,
                         g_audio_buffer_frames);
  if (AudioOutputUnitStart(g_audio_unit) != noErr) return false;
  (void)requested_samples;
  (void)requested_frequency;
  return true;
}

void MacAudio_SetPaused(bool paused) {
  if (!g_audio_unit) return;
  if (paused) AudioOutputUnitStop(g_audio_unit);
  else AudioOutputUnitStart(g_audio_unit);
}

void MacAudio_SetVolume(float volume) { g_audio_volume = volume < 0 ? 0 : volume > 1 ? 1 : volume; }

void MacAudio_Shutdown(void) {
  if (!g_audio_unit) return;
  AudioOutputUnitStop(g_audio_unit);
  AudioUnitUninitialize(g_audio_unit);
  AudioComponentInstanceDispose(g_audio_unit);
  g_audio_unit = NULL;
  free(g_audio_buffer);
  g_audio_buffer = NULL;
  g_audio_buffer_frames = 0;
  g_audio_buffer_cursor = 0;
}

static uint32_t MacGamepad_Buttons(GCExtendedGamepad *pad, int deadzone) {
  uint32_t result = 0;
  if (pad.buttonA.isPressed) result |= 0x0100;
  if (pad.buttonB.isPressed) result |= 0x0001;
  if (pad.buttonX.isPressed) result |= 0x0200;
  if (pad.buttonY.isPressed) result |= 0x0002;
  if (pad.leftShoulder.isPressed) result |= 0x0400;
  if (pad.rightShoulder.isPressed) result |= 0x0800;
  if (pad.buttonMenu.isPressed) result |= 0x0008;
  if (pad.dpad.up.isPressed) result |= 0x0010;
  if (pad.dpad.down.isPressed) result |= 0x0020;
  if (pad.dpad.left.isPressed) result |= 0x0040;
  if (pad.dpad.right.isPressed) result |= 0x0080;
  float x = pad.leftThumbstick.xAxis.value;
  float y = pad.leftThumbstick.yAxis.value;
  float threshold = (float)deadzone / 32767.0f;
  if (x > threshold) result |= 0x0080;
  if (x < -threshold) result |= 0x0040;
  if (y > threshold) result |= 0x0020;
  if (y < -threshold) result |= 0x0010;
  if (pad.leftTrigger.isPressed) result |= 0x0400;
  if (pad.rightTrigger.isPressed) result |= 0x0800;
  return result;
}

void MacGamepad_Poll(bool enable_player1, bool enable_player2, int deadzone,
                     uint32_t *player1, uint32_t *player2,
                     uint32_t *active_players) {
  static dispatch_once_t discovery_once;
  dispatch_once(&discovery_once, ^{
    [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];
  });
  *player1 = *player2 = *active_players = 0;
  NSArray<GCController *> *controllers = GCController.controllers;
  for (NSUInteger i = 0; i < controllers.count && i < 2; ++i) {
    GCController *controller = controllers[i];
    if (!controller.extendedGamepad) continue;
    if (i == 0 && enable_player1) { *player1 = MacGamepad_Buttons(controller.extendedGamepad, deadzone); *active_players |= 1; }
    if (i == 1 && enable_player2) { *player2 = MacGamepad_Buttons(controller.extendedGamepad, deadzone); *active_players |= 2; }
  }
}

#import <AudioToolbox/AudioToolbox.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_metal.h>

#include "macos_backend.h"
#include "config.h"
#include "mmx_rtl.h"
#include "util.h"

typedef struct MetalRenderer {
  SDL_MetalView view;
  CAMetalLayer *layer;
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLRenderPipelineState> pipeline;
  id<MTLSamplerState> sampler;
  id<MTLTexture> textures[3];
  uint8_t *pixels;
  size_t pixels_size;
  int width, height;
  unsigned texture_index;
} MetalRenderer;

static MetalRenderer g_metal;

typedef struct MetalVertex {
  vector_float2 position;
  vector_float2 uv;
} MetalVertex;

static bool MacMetal_Init(SDL_Window *window) {
  memset(&g_metal, 0, sizeof(g_metal));
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
  return true;
}

static void MacMetal_Destroy(void) {
  free(g_metal.pixels);
  g_metal.pixels = NULL;
  g_metal.pixels_size = 0;
  for (int i = 0; i < 3; ++i) g_metal.textures[i] = nil;
  if (g_metal.view) SDL_Metal_DestroyView(g_metal.view);
  memset(&g_metal, 0, sizeof(g_metal));
}

static void MacMetal_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  size_t size = (size_t)width * (size_t)height * 4;
  if (size > g_metal.pixels_size) {
    uint8_t *new_pixels = realloc(g_metal.pixels, size);
    if (!new_pixels) Die("Metal framebuffer allocation failed");
    g_metal.pixels = new_pixels;
    g_metal.pixels_size = size;
  }
  g_metal.width = width;
  g_metal.height = height;
  *pixels = g_metal.pixels;
  *pitch = width * 4;
}

static void MacMetal_CreateTextures(void) {
  MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                          width:g_metal.width
                                                                                         height:g_metal.height
                                                                                      mipmapped:NO];
  descriptor.usage = MTLTextureUsageShaderRead;
  descriptor.storageMode = MTLStorageModeShared;
  for (int i = 0; i < 3; ++i) g_metal.textures[i] = [g_metal.device newTextureWithDescriptor:descriptor];
}

static void MacMetal_EndDraw(void) {
  if (!g_metal.textures[0] || g_metal.textures[0].width != (NSUInteger)g_metal.width ||
      g_metal.textures[0].height != (NSUInteger)g_metal.height)
    MacMetal_CreateTextures();

  id<MTLTexture> texture = g_metal.textures[g_metal.texture_index++ % 3];
  MTLRegion region = MTLRegionMake2D(0, 0, g_metal.width, g_metal.height);
  [texture replaceRegion:region mipmapLevel:0 withBytes:g_metal.pixels bytesPerRow:g_metal.width * 4];

  id<CAMetalDrawable> drawable = [g_metal.layer nextDrawable];
  if (!drawable) return;
  MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = drawable.texture;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

  float viewport_w = (float)g_metal.layer.drawableSize.width;
  float viewport_h = (float)g_metal.layer.drawableSize.height;
  float source_aspect = (float)g_metal.width / (float)g_metal.height;
  float viewport_aspect = viewport_w / viewport_h;
  float sx = 1.0f, sy = 1.0f;
  if (!g_config.ignore_aspect_ratio) {
    if (viewport_aspect > source_aspect) sx = source_aspect / viewport_aspect;
    else sy = viewport_aspect / source_aspect;
  }
  MetalVertex vertices[] = {
    {{-sx,  sy}, {0, 0}}, {{-sx, -sy}, {0, 1}},
    {{ sx,  sy}, {1, 0}}, {{ sx, -sy}, {1, 1}},
  };
  id<MTLCommandBuffer> command = [g_metal.queue commandBuffer];
  id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
  [encoder setRenderPipelineState:g_metal.pipeline];
  [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
  [encoder setFragmentTexture:texture atIndex:0];
  [encoder setFragmentSamplerState:g_metal.sampler atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
  [encoder endEncoding];
  [command presentDrawable:drawable];
  [command commit];
}

static const struct RendererFuncs kMacMetalRendererFuncs = {
  MacMetal_Init, MacMetal_Destroy, MacMetal_BeginDraw, MacMetal_EndDraw
};

void MacMetalRenderer_Create(struct RendererFuncs *funcs) {
  *funcs = kMacMetalRendererFuncs;
}

static AudioUnit g_audio_unit;
static float g_audio_volume = 1.0f;

static OSStatus MacAudio_Render(void *refcon, AudioUnitRenderActionFlags *flags,
                                const AudioTimeStamp *timestamp, UInt32 bus,
                                UInt32 frames, AudioBufferList *data) {
  (void)refcon; (void)flags; (void)timestamp; (void)bus;
  if (data->mNumberBuffers != 1 || !data->mBuffers[0].mData) return noErr;
  RtlRenderAudio((int16_t *)data->mBuffers[0].mData, (int)frames, 2);
  if (g_audio_volume != 1.0f) {
    int16_t *samples = data->mBuffers[0].mData;
    size_t count = (size_t)frames * 2;
    for (size_t i = 0; i < count; ++i) {
      int value = (int)lrintf(samples[i] * g_audio_volume);
      samples[i] = (int16_t)(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
    }
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
  AudioStreamBasicDescription format = {
    .mSampleRate = requested_frequency, .mFormatID = kAudioFormatLinearPCM,
    .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
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
  if (AudioOutputUnitStart(g_audio_unit) != noErr) return false;
  *actual_frequency = requested_frequency;
  *actual_channels = 2;
  *frames_per_block = (534 * requested_frequency + 32040 / 2) / 32040;
  (void)requested_samples;
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
  *player1 = *player2 = *active_players = 0;
  NSArray<GCController *> *controllers = GCController.controllers;
  for (NSUInteger i = 0; i < controllers.count && i < 2; ++i) {
    GCController *controller = controllers[i];
    if (!controller.extendedGamepad) continue;
    if (i == 0 && enable_player1) { *player1 = MacGamepad_Buttons(controller.extendedGamepad, deadzone); *active_players |= 1; }
    if (i == 1 && enable_player2) { *player2 = MacGamepad_Buttons(controller.extendedGamepad, deadzone); *active_players |= 2; }
  }
}

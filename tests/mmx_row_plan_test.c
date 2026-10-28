/* Keep the existing native scenes/jobs and compare planned whole-row output
 * against the independent scalar compositor decisions over randomized data. */
#define MMX_NATIVE_ROW_PLAN 1
#define MMX_NATIVE_TEST_ENTRY existing_native_suite
#include "mmx_native_renderer_test.c"

static void composition_rows(void) {
  static uint16_t backgrounds[3][MMX_RENDER_MAX_WIDTH], objects[MMX_RENDER_MAX_WIDTH];
  static int colors[3][MMX_RENDER_MAX_WIDTH], object_colors[MMX_RENDER_MAX_WIDTH];
  static uint32_t expected[MMX_RENDER_MAX_WIDTH], actual[MMX_RENDER_MAX_WIDTH];
  const unsigned widths[] = {256, 342, 682, MMX_RENDER_MAX_WIDTH};
  for (unsigned sample = 0; sample < 512; ++sample) {
    unsigned width = widths[sample % 4], extra = (width - 256) / 2;
    MmxRenderView view = {(int)width, (int)extra, 16.0/9.0};
    SnesRenderRegisters p = {0}; Raster r = {0};
    p.inidisp = (uint8_t)(row_random() & 15);
    p.screenEnabled[0] = (uint8_t)row_random();
    p.screenEnabled[1] = (uint8_t)row_random();
    p.screenWindowed[0] = (uint8_t)row_random();
    p.screenWindowed[1] = (uint8_t)row_random();
    p.windowsel = row_random() & 0xffffff;
    p.wbgobjlog = (uint16_t)row_random();
    p.window1left = (uint8_t)row_random(); p.window1right = (uint8_t)row_random();
    p.window2left = (uint8_t)row_random(); p.window2right = (uint8_t)row_random();
    if (!(sample % 7)) p.window1left = 0;
    if (!(sample % 11)) p.window2right = 255;
    p.cgwsel = (uint8_t)row_random(); p.cgadsub = (uint8_t)row_random();
    p.fixedColor = (uint16_t)(row_random() & 0x7fff);
    for (unsigned c = 0; c < 256; ++c) r.palette[c] = (uint16_t)(row_random() & 0x7fff);
    for (unsigned x = 0; x < width; ++x) {
      for (unsigned layer = 0; layer < 3; ++layer) {
        unsigned priority = layer == 2 ? (row_random() & 1 ? 15 : 1) :
            layer == 1 ? (row_random() & 1 ? 11 : 7) : (row_random() & 1 ? 12 : 8);
        backgrounds[layer][x] = row_random() & 3 ?
            (uint16_t)(priority << 12 | layer << 8 | (row_random() & 127)) : 0;
        colors[layer][x] = row_random() & 1 ? -1 : (int)(row_random() & 0x7fff);
      }
      objects[x] = row_random() & 3 ? (uint16_t)((row_random() % 4 * 4 + 2) << 12 |
          (row_random() & 1 ? 4 : 6) << 8 | 128 | (row_random() & 127)) : 0;
      object_colors[x] = row_random() & 1 ? -1 : (int)(row_random() & 0xffff);
    }
    unsigned beam_count = sample % 3;
    SnesRenderInterval beams[2];
    for (unsigned i = 0; i < beam_count; ++i) {
      beams[i].left = (int)(row_random() % (width + 100)) - (int)extra - 50;
      beams[i].right = (int)(row_random() % (width + 100)) - (int)extra - 50;
    }
    SnesRenderWindowPlan windows = SnesRenderRegistersWindows(&p,
        p.window1left ? p.window1left : -(int)extra,
        p.window1right == 255 ? 255 + (int)extra : p.window1right,
        p.window2left ? p.window2left : -(int)extra,
        p.window2right == 255 ? 255 + (int)extra : p.window2right);
    SnesRenderComposePlan plan;
    assert(SnesRenderRegistersPlanCompose(&plan, &p, &windows, -(int)extra, width, beams, beam_count));
    assert(plan.count && plan.count <= 11 && plan.spans[0].begin == 0 &&
           plan.spans[plan.count-1].end == width);
    unsigned plain[2]; uint8_t brightness[32];
    for (unsigned c = 0; c < 32; ++c)
      brightness[c] = (uint8_t)(((c << 3) | (c >> 2)) * (p.inidisp & 15) / 15);
    for (unsigned inside = 0; inside < 2; ++inside)
      plain[inside] = condition(p.cgwsel >> 6, inside) ? 0 :
          condition((p.cgwsel >> 4) & 3, inside) ? 127 : (~p.cgadsub & 127);
    bool menu = (sample % 5) == 0, dialogue = (sample % 7) == 0;
    memset(actual, 0, width * sizeof(*actual));
    for (unsigned sx = 0; sx < width; ++sx) {
      int x = (int)sx - (int)extra;
      bool margin = x < 0 || x >= 256;
      if (menu && margin) { expected[sx] = 0; continue; }
      uint16_t screens[2] = {0x500, 0x500}, camouflage_bg = 0x500;
      for (unsigned sub = 0; sub < 2; ++sub) {
        for (unsigned layer = 0; layer < 3; ++layer)
          if ((p.screenEnabled[sub] & (1u << layer)) &&
              (!(p.screenWindowed[sub] & (1u << layer)) || !window(&p, layer, x, extra)) &&
              backgrounds[layer][sx] > screens[sub]) screens[sub] = backgrounds[layer][sx];
        if (!sub) camouflage_bg = screens[0];
        if ((p.screenEnabled[sub] & 16) &&
            (!(p.screenWindowed[sub] & 16) || !window(&p, 4, x, extra)) &&
            objects[sx] > screens[sub]) screens[sub] = objects[sx];
      }
      bool inside = window(&p, 5, x, extra);
      if (beam_count) {
        inside = false;
        for (unsigned i = 0; i < beam_count; ++i)
          inside |= x >= beams[i].left && x <= beams[i].right;
      }
      int bc[] = {colors[0][sx], colors[1][sx], colors[2][sx]};
      expected[sx] = colour(&p, r.palette, brightness, screens[0], screens[1], inside,
          object_colors[sx], bc, dialogue && margin, camouflage_bg);
    }
    native_compose_row(&p, &r, view, backgrounds, colors, objects, object_colors,
        native_brightness(p.inidisp & 15), brightness, plain, &plan, menu, dialogue, actual);
    for (unsigned sx = 0; sx < width; ++sx) if (actual[sx] != expected[sx]) {
      fprintf(stderr, "row plan differs sample=%u width=%u x=%u actual=%08x expected=%08x\n",
          sample, width, sx, actual[sx], expected[sx]);
      abort();
    }
  }
}


#if MMX_NATIVE_BG_SPANS
static void background_span_resource_edges(void) {
  background_resources(); /* Existing independent CHR/palette values. */
  ram[0x1f7a] = 0;
  /* Put CHR and palette changes on different, non-tile-aligned columns. */
  rom_bytes[0x29005]=1; rom_bytes[0x2900c]=1;
  rom_word(0x29006, 0x0853); rom_word(0x2900d, 0x8855);
  MmxRenderAssetsSetRom(NULL,0); MmxRenderAssetsSetRom(rom_bytes,sizeof(rom_bytes));
  MmxBackgroundBinding edge;
  assert(MmxRenderAssetsBindBackground(ram,0x850,1,0x3500,17,true,-1,&edge)==3);
  assert(MmxRenderAssetsBindBackground(ram,0x854,1,0x3500,17,true,-1,&edge)==1);
  assert(MmxRenderAssetsBindBackground(ram,0x857,-1,0x3500,17,true,-1,&edge)==3);
  assert(MmxRenderAssetsBindBackground(ram,0x850,1,0x3500,17,true,1,&edge)==5);
  const int starts[] = {-5,-1,0,1,0x84d,0x852,0x853,0x854,0x855,0x856,8190,8191,8192,8196};
  for (unsigned s = 0; s < sizeof(starts)/sizeof(*starts); ++s)
    for (int step = -2; step <= 2; ++step)
      for (unsigned fixed = 0; fixed < 2; ++fixed) {
        MmxBackgroundBinding binding;
        unsigned count = MmxRenderAssetsBindBackground(ram,starts[s],step,0x3500,17,true,
            fixed ? 1 : -1,&binding);
        assert(count && count <= 17);
        for (unsigned i = 0; i < count; ++i) {
          int x = starts[s]+(int)i*step;
          assert(binding.tile == MmxRenderAssetsBackgroundTile(ram,x,0x3500));
          const MmxBackgroundPalette *pal = fixed ? MmxRenderAssetsBackgroundPalettePhase(ram,1) :
              MmxRenderAssetsBackgroundPalette(ram,x);
          assert(binding.palette == pal);
        }
      }
}
static void background_span_rows(void) {
  static uint16_t actual[MMX_RENDER_MAX_WIDTH], expected[MMX_RENDER_MAX_WIDTH];
  static int colors[MMX_RENDER_MAX_WIDTH], reference_colors[MMX_RENDER_MAX_WIDTH];
  const int cameras[] = {0, 1, 127, 255, 256, 0x500, 0x700, 0xaff, 0xb00,
      0xbff, 0xc00, 0x1eff, 0x1f00, 0x2000};
  /* Valid random source maps retain authored coordinate/table structure.
   * Resource-phase edge cases are also exercised by the existing scene suite. */
  MmxRendererReset(); MmxRendererSetRom(NULL, 0);
  memset(rom_bytes, 0, sizeof(rom_bytes));
  for (unsigned i = 0; i < 0x10000; ++i) rom_bytes[i] = (uint8_t)row_random();
  for (unsigned i = 0x8000; i < 0x10000; ++i) rom_bytes[i] = (uint8_t)row_random();
  MmxRendererSetRom(rom_bytes, sizeof(rom_bytes));
  memset(&frame_rush, 0, sizeof(frame_rush));
  memset(&frame_weapons, 0, sizeof(frame_weapons));
  Raster raster = {0}; raster.vram = ppu.vram;
  for (unsigned i = 0; i < 0x8000; ++i) ppu.vram[i] = (uint16_t)row_random();
  for (unsigned i = 0; i < 0x20000; ++i) frame.ram[i] = (uint8_t)row_random();
  /* Point both map descriptor tables into valid, randomized ROM banks. */
  frame.ram[0xb95] = frame.ram[0xb98] = 0;
  frame.ram[0xb96] = frame.ram[0xb99] = 0x80;
  frame.ram[0xb97] = frame.ram[0xb9a] = 0x80;
  for (unsigned sample = 0; sample < 1536; ++sample) {
    unsigned layer = sample % 3, stage = (sample / 3) % 13;
    frame.ram[0x1f7a] = (uint8_t)stage;
    frame.ram[0x1e89] = (uint8_t)((sample / 39) % 9 * 2);
    frame.ram[0x1f3f] = (uint8_t)(sample & 1);
    frame.ram[0x1f96] = (uint8_t)(sample & 0x40);
    for (unsigned c = 0; c < 4; ++c) {
      unsigned address = (unsigned[]){0x1e4d, 0x1e50, 0x1e8d, 0x1e90}[c];
      unsigned value = (unsigned)cameras[(sample / (c + 1)) % (sizeof(cameras)/sizeof(*cameras))];
      frame.ram[address] = (uint8_t)value; frame.ram[address+1] = (uint8_t)(value >> 8);
    }
    /* Some foreground views genuinely straddle the authored right edge. */
    rom_long(0x30d24 + stage * 3, 0x81f000);
    rom_bytes[0xf000] = (uint8_t)(1 + sample % 32);
    ppu.bgmode = (uint8_t)(1 | ((sample % 17 == 0) ? 0x70 : 0));
    ppu.bgTileAdr = (uint16_t)row_random();
    ppu.bgXsc[layer] = (uint8_t)row_random();
    ppu.hScroll[layer] = (uint16_t)(row_random() & 1023);
    ppu.vScroll[layer] = (uint16_t)(row_random() & 1023);
    ppu.screenEnabled[0] = (uint8_t)(sample & 1 ? 7 : 3);
    ppu.screenEnabled[1] = 0; ppu.cgwsel = 2; ppu.cgadsub = 0x44;
    view_dx = sample & 2 ? cameras[sample % (sizeof(cameras)/sizeof(*cameras))] - 256 : 0;
    view_dy = sample & 4 ? (int)(sample % 1024) - 512 : 0;
    airport_upper_sky = stage == 5 && (sample & 8);
    airport_sky_width = stage == 5 && !airport_upper_sky ? 640 : 0;
    submarine_count = stage == 1 ? 2 : 0;
    submarine_bodies[0] = (SubmarineBody){0xb00, 0x100, 0x180, 0x28, true, true};
    submarine_bodies[1] = (SubmarineBody){0x700, 0x200, 0x280, 0x20, true, false};
    unsigned width = (unsigned[]){256,342,682,1024}[sample % 4];
    int extra = (int)(width - 256) / 2, y = (int)(sample % 1024);
    g_mmx_render_asset_repairs = (sample & 1) != 0;
    for (unsigned i = 0; i < width; ++i) colors[i] = reference_colors[i] = -1;
    memset(door_cache, 0, sizeof(door_cache));
    for (unsigned i = 0; i < 2; ++i) stage_tile_cache[i].key = ~0u;
    SnesRenderRegisters registers = SnesRenderRegistersLoad(&ppu);
    for (unsigned i = 0; i < width; ++i)
      expected[i] = background(&registers, &raster, layer, (int)i - extra, y, true, &reference_colors[i]);
    for (unsigned i = 0; i < width;) {
      unsigned count = background_source_span(&registers, &raster, layer, (int)i - extra,
          y, true, width - i, actual + i, colors + i);
      assert(count && count <= width - i); i += count;
    }
    for (unsigned i = 0; i < width; ++i) if (actual[i] != expected[i] || colors[i] != reference_colors[i]) {
      fprintf(stderr,"background span mismatch sample=%u stage=%u layer=%u x=%d y=%d pixel=%04x/%04x color=%d/%d\n",
          sample,stage,layer,(int)i-extra,y,actual[i],expected[i],colors[i],reference_colors[i]); abort();
    }
  }
  MmxRendererReset(); MmxRendererSetRom(NULL, 0);
  puts("BACKGROUND SOURCE SPANS: 1536 SCALAR-COMPARED FULL-WIDTH ROWS PASS");
}
#endif

int main(void) {
#if MMX_NATIVE_BG_SPANS
  background_span_resource_edges();
  background_span_rows();
#endif
  existing_native_suite();
  composition_rows();
  puts("WHOLE-VIEW ROW PLAN: NATIVE SCENES AND 512 RANDOMIZED COMPOSITION ROWS PASS");
  return 0;
}

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
    Ppu p = {0}; Raster r = {0};
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
    SnesRenderWindowPlan windows = SnesRenderWindows(&p,
        p.window1left ? p.window1left : -(int)extra,
        p.window1right == 255 ? 255 + (int)extra : p.window1right,
        p.window2left ? p.window2left : -(int)extra,
        p.window2right == 255 ? 255 + (int)extra : p.window2right);
    SnesRenderComposePlan plan;
    assert(SnesRenderPlanCompose(&plan, &p, &windows, -(int)extra, width, beams, beam_count));
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

int main(void) {
  existing_native_suite();
  composition_rows();
  puts("WHOLE-VIEW ROW PLAN: NATIVE SCENES AND 512 RANDOMIZED COMPOSITION ROWS PASS");
  return 0;
}

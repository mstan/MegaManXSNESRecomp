/* Included after the compositor's scalar helpers. All data is private to this
 * captured row/view. This pipeline never reads or writes the live machine. */
#include "snes/raster_plan.h"
#include <assert.h>
#if MMX_NATIVE_BG_SPANS
#include "mmx_background_span.inc"
#endif

static void native_background_range(const Ppu *p, const Raster *r,
    unsigned layer, int y, bool stage, int extra, unsigned begin, unsigned end,
    uint16_t *pixels, int *colors) {
  int by = y;
  unsigned mosaic = (p->mosaic & (1u << layer)) ? (p->mosaic >> 4) + 1 : 0;
  if (mosaic) by -= by % (int)mosaic;
#if MMX_NATIVE_BG_SPANS
  if (!mosaic) {
    for (unsigned sx = begin; sx < end;) {
      unsigned count = background_source_span(p, r, layer, (int)sx - extra,
          by, stage, end - sx, pixels + sx, colors + sx);
      assert(count && count <= end - sx);
      if (!count || count > end - sx) {
        pixels[sx] = background(p, r, layer, (int)sx - extra, by, stage, &colors[sx]);
        count = 1;
      }
      sx += count;
    }
    return;
  }
#endif
  for (unsigned sx = begin; sx < end; ++sx) {
    int x = (int)sx - extra;
    if (mosaic) x -= ((x % (int)mosaic) + (int)mosaic) % (int)mosaic;
    pixels[sx] = background(p, r, layer, x, by, stage, &colors[sx]);
  }
}

static void native_compose_row(const Ppu *p, const Raster *r,
    MmxRenderView view, const uint16_t backgrounds[3][MMX_RENDER_MAX_WIDTH],
    const int colors[3][MMX_RENDER_MAX_WIDTH], const uint16_t *objects,
    const int *object_colors, const uint32_t *rgb, const uint8_t brightness[32],
    const unsigned plain_layers[2], const SnesRenderComposePlan *plan,
    bool menu, bool dialogue_backdrop, uint32_t *out) {
  (void)view;
  for (unsigned s = 0; s < plan->count; ++s) {
    const SnesRenderComposeSpan *span = &plan->spans[s];
    if (menu && span->margin) continue; /* The output starts cleared. */
    const unsigned plain = plain_layers[span->color_window];
    for (unsigned sx = span->begin; sx < span->end; ++sx) {
      uint16_t main = 0x500, sub = 0x500;
      uint16_t b0 = backgrounds[0][sx], b1 = backgrounds[1][sx], b2 = backgrounds[2][sx];
      uint16_t candidate = b0 & span->main[0];
      if (candidate > main) main = candidate;
      candidate = b1 & span->main[1];
      if (candidate > main) main = candidate;
      candidate = b2 & span->main[2];
      if (candidate > main) main = candidate;
      uint16_t camouflage_bg = main;
      candidate = objects[sx] & span->main[3];
      if (candidate > main) main = candidate;
      candidate = b0 & span->sub[0];
      if (candidate > sub) sub = candidate;
      candidate = b1 & span->sub[1];
      if (candidate > sub) sub = candidate;
      candidate = b2 & span->sub[2];
      if (candidate > sub) sub = candidate;
      candidate = objects[sx] & span->sub[3];
      if (candidate > sub) sub = candidate;
      int bg_colors[3] = {colors[0][sx], colors[1][sx], colors[2][sx]};
      unsigned layer = (main >> 8) & 15;
      bool camouflage = object_colors[sx] >= 0 && (object_colors[sx] & 0x8000) &&
          (layer == 4 || layer == 6);
      if ((plain & (1u << layer)) && !camouflage)
        out[sx] = native_plain_colour(r->palette, rgb, main, object_colors[sx], bg_colors);
      else
        out[sx] = colour(p, r->palette, brightness, main, sub, span->color_window,
            object_colors[sx], bg_colors, dialogue_backdrop && span->margin, camouflage_bg);
    }
  }
}

static void native_render_row(const Ppu *p, const Raster *r, int y,
    MmxRenderView view, bool stage, bool menu, bool dialogue_backdrop,
    int ripple, unsigned palette_fade, const uint16_t center[3][256],
    unsigned center_layers, const uint16_t *objects, int *object_colors,
    const uint32_t *rgb, const uint8_t brightness[32], const unsigned plain_layers[2],
    const LightBeam beams[2], unsigned beam_count, uint32_t *out) {
  uint16_t backgrounds[3][MMX_RENDER_MAX_WIDTH];
  int colors[3][MMX_RENDER_MAX_WIDTH];
  for (unsigned layer = 0; layer < 3; ++layer) {
    memset(colors[layer], 0xff, (size_t)view.width * sizeof(colors[layer][0]));
    if (!((p->screenEnabled[0] | p->screenEnabled[1]) & (1u << layer))) {
      memset(backgrounds[layer], 0, (size_t)view.width * sizeof(backgrounds[layer][0]));
      continue;
    }
    int by = y + 1 + ((stage && !layer) ? ripple : 0);
    if (center_layers & (1u << layer)) {
      memcpy(backgrounds[layer] + view.extra, center[layer], 256 * sizeof(uint16_t));
      native_background_range(p, r, layer, by, stage, view.extra, 0,
          (unsigned)view.extra, backgrounds[layer], colors[layer]);
      native_background_range(p, r, layer, by, stage, view.extra,
          (unsigned)view.extra + 256, (unsigned)view.width, backgrounds[layer], colors[layer]);
    } else {
      native_background_range(p, r, layer, by, stage, view.extra, 0,
          (unsigned)view.width, backgrounds[layer], colors[layer]);
    }
    if (palette_fade) for (int sx = 0; sx < view.width; ++sx)
      if (colors[layer][sx] >= 0)
        colors[layer][sx] = MmxRenderAssetsFadeColor((uint16_t)colors[layer][sx], palette_fade);
  }
  if (palette_fade) for (int sx = 0; sx < view.width; ++sx)
    if (object_colors[sx] >= 0)
      object_colors[sx] = MmxRenderAssetsFadeColor((uint16_t)object_colors[sx], palette_fade) |
          (object_colors[sx] & 0x8000);
  SnesRenderWindowPlan windows = SnesRenderWindows(p,
      p->window1left == 0 ? -view.extra : p->window1left,
      p->window1right == 255 ? 255 + view.extra : p->window1right,
      p->window2left == 0 ? -view.extra : p->window2left,
      p->window2right == 255 ? 255 + view.extra : p->window2right);
  SnesRenderInterval color_windows[2];
  for (unsigned i = 0; i < beam_count; ++i)
    color_windows[i] = (SnesRenderInterval){beams[i].left[y], beams[i].right[y]};
  SnesRenderComposePlan plan;
  bool planned = SnesRenderPlanCompose(&plan, p, &windows, -view.extra,
      (unsigned)view.width, color_windows, beam_count);
  assert(planned); /* Validated renderer dimensions and at most two beams. */
  if (!planned) return;
  native_compose_row(p, r, view, backgrounds, colors, objects, object_colors,
      rgb, brightness, plain_layers, &plan, menu, dialogue_backdrop, out);
}

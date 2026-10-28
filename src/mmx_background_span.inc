/* Full-view background source spans. The scalar background() above remains
 * the independent build/reference path. All bindings belong to this captured
 * row; no persistent cache or live machine state is introduced. */
static void background_span_cut(unsigned *count, int coordinate, int step, int edge) {
  int distance = step > 0 && edge > coordinate ? (edge - coordinate + step - 1) / step :
      step < 0 && edge <= coordinate ? (coordinate - edge) / -step + 1 : 0;
  if (distance > 0 && (unsigned)distance < *count) *count = (unsigned)distance;
}

static unsigned background_source_span(const SnesRenderRegisters *p, const Raster *r,
    unsigned layer, int x, int y, bool stage, unsigned count,
    uint16_t *pixels, int *colors) {
  static const unsigned low[] = {8, 7, 1}, high[] = {12, 11, 3};
  const unsigned stage_id = frame.ram[0x1f7a];
  /* Menus have per-pixel imported icons. The Rush ceiling is a separately
   * reconstructed image, not a tiled source binding. */
  if (!stage || (layer == 0 && stage_id == 4 &&
      (frame_rush.mode == MMX_RUSH_PLAYING || frame_rush.mode == MMX_RUSH_PREPARING))) {
    for (unsigned i = 0; i < count; ++i)
      pixels[i] = background(p, r, layer, x + (int)i, y, stage, &colors[i]);
    return count;
  }
  background_span_cut(&count, x, 1, 0);
  background_span_cut(&count, x, 1, 256);
  bool margin = x < 0 || x >= 256;
  bool water = stage_id == 1 && (p->screenEnabled[0] & 4) &&
      !(p->screenEnabled[1] & 4) && (p->cgwsel & 2) && (p->cgadsub & 0x44) == 0x44;
  if (layer == 2 && margin && !water) {
    bool blizzard = frame.ram[0x1f3f] && stage_id == 8;
    if (frame.ram[0x1f3f] && frame_rush.mode == MMX_RUSH_PLAYING)
      for (unsigned i = 0; i < 2; ++i)
        blizzard |= frame_rush.bosses[i].id == 0 && frame_rush.bosses[i].phase != 0;
    if (!blizzard) { memset(pixels, 0, count * sizeof(*pixels)); return count; }
  }
  bool slime_surface = layer == 1 && stage_id == 6 && frame.ram[0x1e89] == 0x0c;
  bool lower_shaft = false;
  unsigned bpp = layer == 2 ? 2 : 4, size = PPU_bigTiles(p, layer) ? 16 : 8;
  int px = (x + p->hScroll[layer]) & 1023, py = (y + p->vScroll[layer]) & 1023;
  if (layer == 2 && water) py = (py + view_dy) & 1023;
  unsigned sc = p->bgXsc[layer], tx = px / size, ty = py / size;
  unsigned a = (sc & 0xfc) * 256 + (tx & 31) + (ty & 31) * 32;
  if ((sc & 1) && (tx & 32)) a += 1024;
  if ((sc & 2) && (ty & 32)) a += (sc & 1) ? 2048 : 1024;
  uint16_t tile = r->vram[a & 0x7fff];
  int step = 1, asset_x = -1, asset_step = 0;
  if (size == 8 && layer < 2 && !slime_surface &&
      (margin || view_dx || view_dy || airport_upper_sky)) {
    int wx, wy;
    if (layer == 0) {
      wx = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e4d), p->hScroll[0]) + view_dx + x;
      wy = MmxDisplay_ExpandStageScroll((uint16_t)word(frame.ram, 0x1e50), p->vScroll[0]) + view_dy + y;
      if (stage_id == 3 && word(frame.ram, 0x1e4d) == 0x1f00 &&
          word(frame.ram, 0x1e50) >= 0x500 && word(frame.ram, 0x1e50) <= 0x600 && wy >= 0x600) {
        wx -= 0x1e00; wy += 0x200; lower_shaft = true;
      }
      for (unsigned i = 0; i < submarine_count; ++i) {
        const SubmarineBody *body = &submarine_bodies[i];
        if (body->scrolling && (p->vScroll[0] & 1023) == (word(frame.ram, 0xc4) & 1023)) {
          background_span_cut(&count, wx, step, body->left);
          background_span_cut(&count, wx, step, body->left + 160);
          if (wx < body->left || wx >= body->left + 160) wy += body->shift;
          break;
        }
      }
    } else {
      int stream_x = (int16_t)word(frame.ram, 0x1e8d), stream_y = (int16_t)word(frame.ram, 0x1e90);
      wx = stream_x + (((p->hScroll[1] - stream_x + 512) & 1023) - 512) + x;
      wy = stream_y + (((p->vScroll[1] - stream_y + 512) & 1023) - 512) + y;
      unsigned mode = frame.ram[0x1e89];
      int nx = word(frame.ram, 0x1e4d), ny = word(frame.ram, 0x1e50);
      if (mode == 2 || mode == 14) wx += ((nx + view_dx) >> 1) - (nx >> 1);
      else if (mode == 4 || mode == 16) wx += view_dx;
      if (mode == 2 || mode == 16) wy += ((ny + view_dy) >> 1) - (ny >> 1);
      else if (mode == 4 || mode == 14) wy += view_dy;
      if (stage_id == 1 && mode == 0x0c) {
        background_span_cut(&count, wx, step, 0xb00);
        background_span_cut(&count, wx, step, 0xc00);
        if (wx < 0xb00 || wx >= 0xc00 || wy < 0 || wy >= 256) {
          memset(pixels, 0, count * sizeof(*pixels)); return count;
        }
      }
      if (stage_id == 1 && mode == 0x0e && wy >= 0x370 && wy < 0x390) {
        background_span_cut(&count, wx, step, 0x500);
        background_span_cut(&count, wx, step, 0x700);
        if (wx >= 0x500 && wx < 0x700) wx = 0x700 | (wx & 255);
      }
      if (stage_id == 1 && mode == 0x0e && stream_x >= 0xc00 && stream_x < 0xe00) {
        background_span_cut(&count, wx, step, 0xc00);
        if (wx < 0xc00) { wx = 0xc00; step = 0; }
      }
      if (stage_id == 0 && stream_x >= 0xa00) {
        background_span_cut(&count, wx, step, 0xa00);
        if (wx < 0xa00) { wx = 0xa00; step = 0; }
      }
      if (airport_upper_sky) { wx = 0x300 + ((unsigned)wx & 255); wy = (unsigned)wy & 255; step = 1; }
    }
    background_span_cut(&count, wx, step, 0);
    if (wx < 0) { wx = -wx - 1; step = -step; }
    if (layer == 1 && airport_sky_width) {
      background_span_cut(&count, wx, step, airport_sky_width);
      if (wx >= airport_sky_width) {
        wx %= 2 * airport_sky_width;
        background_span_cut(&count, wx, step, 0);
        background_span_cut(&count, wx, step, airport_sky_width);
        background_span_cut(&count, wx, step, 2 * airport_sky_width);
        if (wx >= airport_sky_width) { wx = 2 * airport_sky_width - wx - 1; step = -step; }
      }
    }
    if (layer == 0) {
      if (stage_id < 13 && rom_size > 0x30d24 + stage_id * 3 + 2) {
        const uint8_t *t = rom + 0x30d24 + stage_id * 3;
        const uint8_t *bounds = rom_at(word(t, 0) | (t[2] << 16), 2);
        if (bounds && bounds[0] && bounds[0] <= 32) {
          int width = bounds[0] * 256;
          background_span_cut(&count, wx, step, width);
          if (wx >= width) { wx = width * 2 - wx - 1; step = -step; }
          background_span_cut(&count, wx, step, 0);
          if (wx < 0) { wx = 0; step = 0; }
        }
      }
      for (unsigned i = 0; i < submarine_count; ++i) {
        const SubmarineBody *body = &submarine_bodies[i];
        if (body->hidden && wy >= body->top && wy < body->bottom) {
          background_span_cut(&count, wx, step, body->left);
          background_span_cut(&count, wx, step, body->left + 160);
          if (wx >= body->left && wx < body->left + 160) { wx -= 256; break; }
        }
      }
      /* Door ownership changes only at a 16px map cell. Eight-pixel source
       * boundaries also cover that transition, for either traversal direction. */
      unsigned cell = step < 0 ? ((unsigned)wx & 7) + 1 : step > 0 ? 8 - ((unsigned)wx & 7) : count;
      if (cell < count) count = cell;
      if (door_body(wx, wy)) {
        bool left = door_body(wx - 16, wy), right = door_body(wx + 16, wy);
        if (left || right) {
          int boundary = (wx & ~15) + (right ? 16 : 0);
          bool view_left = view_camera(frame.ram, 0x1e4d) + 128 < boundary;
          if (view_left && left) wx += 16;
          if (!view_left && right) wx -= 16;
        }
      }
    }
    background_span_cut(&count, wx, step, 0);
    background_span_cut(&count, wx, step, 8192);
    unsigned cell = step < 0 ? ((unsigned)wx & 7) + 1 : step > 0 ? 8 - ((unsigned)wx & 7) : count;
    if (cell < count) count = cell;
    uint16_t mapped;
    if (stage_tile_for_draw(layer, wx, wy, &mapped)) {
      tile = mapped; px = wx; py = wy;
      bool half = layer == 1 && (stage_id <= 2 || stage_id == 4);
      asset_x = half ? wx * 2 - 128 : wx;
      asset_step = half ? step * 2 : step;
      if (layer == 1 && stage_id == 1 && frame.ram[0x1e89] == 0x0e && word(frame.ram, 0x1e8d) >= 0xc00) {
        background_span_cut(&count, asset_x, asset_step, 0x1a20);
        if (asset_x >= 0x1a20) { asset_x = 0x1a1f; asset_step = 0; }
      }
      if (layer == 1 && (stage_id == 8 || (stage_id == 0 && frame.ram[0x1e89] == 0x0c))) {
        asset_x = -1; asset_step = 0;
      }
    } else {
      if (airport_upper_sky) { memset(pixels, 0, count * sizeof(*pixels)); return count; }
      step = 1; /* Unmapped source retains the original circular VRAM binding. */
    }
  }
  unsigned cell = step < 0 ? ((unsigned)px & 7) + 1 : step > 0 ? 8 - ((unsigned)px & 7) : count;
  if (cell < count) count = cell;
  int cx = px & (size - 1), cy = py & (size - 1);
  if (tile & 0x4000) cx = size - 1 - cx;
  if (tile & 0x8000) cy = size - 1 - cy;
  unsigned number = ((tile & 1023) + cx / 8 + cy / 8 * 16) & 1023;
  unsigned address = (PPU_bgTileAdr(p, layer) + number * bpp * 4) & 0x7fff;
  const uint8_t *bits = NULL;
  const MmxBackgroundPalette *palette = NULL;
  if (g_mmx_render_asset_repairs) {
    MmxBackgroundBinding binding;
    count = MmxRenderAssetsBindBackground(frame.ram, asset_x, asset_step, address, count,
        bpp == 4 && !(stage_id == 1 && layer == 1), lower_shaft ? 1 : -1, &binding);
    bits = binding.tile; palette = binding.palette;
  }
  unsigned p01, p23 = 0;
  if (bits) {
    bits += (cy & 7) * 2;
    p01 = bits[0] | ((unsigned)bits[1] << 8);
    p23 = bits[16] | ((unsigned)bits[17] << 8);
  } else {
    unsigned row_address = (address + (cy & 7)) & 0x7fff;
    p01 = r->vram[row_address];
    if (bpp == 4) p23 = r->vram[(row_address + 8) & 0x7fff];
  }
  unsigned priority = tile & 0x2000 ? (layer == 2 && (p->bgmode & 8) ? 15 : high[layer]) : low[layer];
  unsigned base = (((tile >> 10) & 7) << bpp);
  unsigned prefix = (priority << 12) | (layer << 8) | base;
  int bit = 7 - (cx & 7), bit_step = (tile & 0x4000) ? step : -step;
  for (unsigned i = 0; i < count; ++i, bit += bit_step) {
    unsigned pixel = ((p01 >> bit) & 1) | (((p01 >> (bit + 8)) & 1) << 1);
    if (bpp == 4) pixel |= (((p23 >> bit) & 1) << 2) | (((p23 >> (bit + 8)) & 1) << 3);
    pixels[i] = pixel ? (uint16_t)(prefix | pixel) : 0;
    unsigned index = base | pixel;
    if (pixel && palette && palette->valid[index]) colors[i] = palette->colors[index];
  }
  return count;
}

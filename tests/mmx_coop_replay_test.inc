/* Co-op replays: power-on, then the inputs a netplay log recorded for each
 * seat (tools/coop_replay_from_csv.py). The netplay simulation is
 * deterministic, so a replay with the recorded mods and display settings
 * follows the session tick for tick; checkpoints prove it. */
enum { COOP_REPLAY_MAX = 20000, COOP_REPLAY_CHECKS = 2000 };
typedef struct CoopReplay {
  unsigned ticks, checks;
  uint16_t p1[COOP_REPLAY_MAX], p2[COOP_REPLAY_MAX];
  struct { unsigned tick, x[2], y[2]; } check[COOP_REPLAY_CHECKS];
} CoopReplay;
static void coop_replay_load(const char *path, CoopReplay *r) {
  FILE *f = fopen(path, "r");
  check(f != NULL, "co-op replay file opens");
  char line[256];
  r->ticks = r->checks = 0;
  while (fgets(line, sizeof(line), f)) {
    unsigned a, b, c, d, e;
    if (line[0] == 'R' && sscanf(line + 1, "%u %u %u", &a, &b, &c) == 3) {
      if (r->ticks + a > COOP_REPLAY_MAX) { fprintf(stderr, "FAIL: co-op replay is too long\n"); exit(1); }
      for (unsigned i = 0; i < a; ++i) { r->p1[r->ticks] = (uint16_t)b; r->p2[r->ticks++] = (uint16_t)c; }
    } else if (line[0] == 'C' && sscanf(line + 1, "%u %u %u %u %u", &a, &b, &c, &d, &e) == 5) {
      if (r->checks >= COOP_REPLAY_CHECKS) { fprintf(stderr, "FAIL: too many co-op replay checkpoints\n"); exit(1); }
      r->check[r->checks].tick = a;
      r->check[r->checks].x[0] = b; r->check[r->checks].y[0] = c;
      r->check[r->checks].x[1] = d; r->check[r->checks].y[1] = e;
      ++r->checks;
    }
  }
  fclose(f);
  check(r->ticks > 0, "co-op replay has inputs");
}
static const uint8_t *coop_replay_body(unsigned seat) {
  static MmxCoopState s;
  static uint8_t none[0x90];
  s = MmxCoopGetState();
  if (!s.initialized) return none;
  return seat == s.current ? g_ram + 0xba8 : s.players[seat].body;
}
static unsigned coop_replay_x(unsigned seat) { const uint8_t *b = coop_replay_body(seat); return b[5] | b[6] << 8; }
static unsigned coop_replay_y(unsigned seat) { const uint8_t *b = coop_replay_body(seat); return b[8] | b[9] << 8; }
/* The compositor flag and view width feed culling and spawn margins, so a
 * replay must match the session: Zero enabled, Widescreen = 0. */
static void coop_replay_start(const char *assets) {
  check(assets && MmxZeroLoad(assets), "replay loads owned X3 Zero");
  MmxZeroRegisterHooks(); MmxCoopRegisterHooks();
  check(MmxCoopEnable(0), "replay enables co-op with P1 as X");
  g_config.widescreen = false;
  g_mmx_custom_renderer = true;
  g_mmx_custom_view = (MmxRenderView){256, 0, 4.0 / 3.0};
}
/* Runs ticks [from, to) and returns the first checkpoint tick in that range
 * whose bodies differ from the recording, or UINT_MAX. */
static unsigned coop_replay_run(const CoopReplay *r, unsigned from, unsigned to) {
  unsigned diverged = UINT_MAX, next = 0;
  while (next < r->checks && r->check[next].tick < from) ++next;
  for (unsigned t = from; t < to && t < r->ticks; ++t) {
    coop_frames(1, r->p1[t], r->p2[t]);
    if (next < r->checks && r->check[next].tick == t) {
      for (unsigned seat = 0; seat < 2 && diverged == UINT_MAX; ++seat)
        if (coop_replay_x(seat) != r->check[next].x[seat] || coop_replay_y(seat) != r->check[next].y[seat]) {
          diverged = t;
          fprintf(stderr, "replay diverges by tick %u seat %u: %u,%u vs recorded %u,%u\n", t, seat,
                  coop_replay_x(seat), coop_replay_y(seat), r->check[next].x[seat], r->check[next].y[seat]);
        }
      ++next;
    }
  }
  return diverged;
}
static void coop_replay_probe(const char *assets, const char *path) {
  static CoopReplay r;
  coop_replay_load(path, &r);
  coop_replay_start(assets);
  unsigned diverged = coop_replay_run(&r, 0, r.ticks);
  fprintf(stderr, "replay: %u ticks, %u checkpoints, %s; end P1 %u,%u P2 %u,%u\n", r.ticks, r.checks,
          diverged == UINT_MAX ? "no divergence" : "diverged",
          coop_replay_x(0), coop_replay_y(0), coop_replay_x(1), coop_replay_y(1));
}

/* Highway after Bee Blader (tests/data/coop_highway_collapse.replay): one
 * controller drove both seats. The road (enemy $22) and the falling slab
 * (item $08) carried X; P2 had no rider pass, fell through the slab and
 * ended inside it on the lower road at y=591, unable to move. */
static void coop_collapse_checks(const char *assets, const char *path) {
  static CoopReplay r;
  coop_replay_load(path, &r);
  coop_replay_start(assets);
  /* The recording predates the partner's pass through the slab's drop frame
   * (slab_drop_hook), so it matches only up to the last checkpoint before
   * the drop. */
  check(coop_replay_run(&r, 0, 3751) == UINT_MAX,
        "replay follows the recorded session up to the falling slab");
  for (unsigned t = 3751; t < 3787; ++t) coop_frames(1, r.p1[t], r.p2[t]);
  unsigned deepest = 0;
  for (unsigned t = 3787; t < 3870; ++t) {
    coop_frames(1, r.p1[t], r.p2[t]);
    if (coop_replay_y(1) > deepest) deepest = coop_replay_y(1);
  }
  fprintf(stderr, "collapse: P1 %u,%u P2 %u,%u, P2 deepest %u\n",
          coop_replay_x(0), coop_replay_y(0), coop_replay_x(1), coop_replay_y(1), deepest);
  check(coop_replay_y(1) == coop_replay_y(0) && (coop_replay_body(1)[0x2b] & 4),
        "P2 lands on the same lower road as X");
  check(deepest < 591, "P2 never sinks into the slab");
  coop_image(0, "highway-collapse");
  unsigned before = coop_replay_x(1);
  for (unsigned t = 3870; t < 3960; ++t) coop_frames(1, r.p1[t], r.p2[t]);
  check(coop_replay_x(1) + 16 < before, "P2 walks left after landing");
  puts("MMX CO-OP HIGHWAY COLLAPSE CHECKS PASSED");
}

/* Highway's slab drop (tests/data/coop_highway_slab_drop.replay): both seats
 * stood on the road when it gave way at tick 4019. The slab's one-frame drop
 * state ($82:E62A) moves a grounded world body down 2 px and latches it as a
 * rider; only X ever got it, so P2 left the road a tick early, reached the
 * slab five ticks ahead of X, and the same jump press landed on X's native
 * landing frame (dropped) but not on P2's. slab_drop_hook runs the block for
 * the partner too. */
static void coop_slab_drop_checks(const char *assets, const char *path) {
  static CoopReplay r;
  coop_replay_load(path, &r);
  coop_replay_start(assets);
  check(coop_replay_run(&r, 0, 4019) == UINT_MAX,
        "replay follows the recorded session up to the slab's drop");
  unsigned y0[2] = {coop_replay_y(0), coop_replay_y(1)};
  coop_frames(1, r.p1[4019], r.p2[4019]);
  fprintf(stderr, "slab drop: X %u -> %u, P2 %u -> %u\n", y0[0], coop_replay_y(0), y0[1], coop_replay_y(1));
  check(coop_replay_y(0) == y0[0] + 2 && coop_replay_y(1) == y0[1] + 2,
        "the drop frame moves both grounded bodies down with the slab");
  unsigned fell[2] = {0, 0};
  for (unsigned t = 4020; t < 4040; ++t) {
    coop_frames(1, r.p1[t], r.p2[t]);
    for (unsigned seat = 0; seat < 2; ++seat)
      if (!fell[seat] && coop_replay_body(seat)[2] == 0x08) fell[seat] = t;
  }
  fprintf(stderr, "slab drop: X falls at %u, P2 at %u\n", fell[0], fell[1]);
  check(fell[0] && fell[0] == fell[1], "both seats leave the slab on the same tick");
  puts("MMX CO-OP HIGHWAY SLAB DROP CHECKS PASSED");
}

/* Chill Penguin's canister ($4D), dropped by a hovering carrier, is solid
 * through $82:D7D7 and sets side contact through $84:9A02, both on the world
 * body only. From a private co-op checkpoint beside one (MMX_COOP_CANISTER_
 * FIXTURE), each seat walks left into it and must stop where X does. */
static void coop_canister_checks(const char *assets, const char *fixture) {
  coop_replay_start(assets);
  unsigned stop[2] = {0, 0};
  for (unsigned seat = 0; seat < 2; ++seat) {
    check(RtlLoadSnapshot(fixture), "canister fixture loads");
    unsigned can = 0;
    for (unsigned d = 0xe68; d < 0x1228; d += 64) if (g_ram[d] && g_ram[d+10] == 0x4d) can = d;
    check(can != 0, "fixture contains a canister");
    unsigned cx = g_ram[can+5] | g_ram[can+6] << 8;
    check(coop_replay_x(seat) > cx, "the seat starts right of the canister");
    stop[seat] = 99999;
    for (unsigned t = 0; t < 120; ++t) {
      coop_frames(1, seat == 0 ? SNES_PAD_LEFT : 0, seat == 1 ? SNES_PAD_LEFT : 0);
      if (coop_replay_x(seat) < stop[seat]) stop[seat] = coop_replay_x(seat);
    }
    fprintf(stderr, "canister x%u: seat %u stops at x%u ($0BD4=%02x)\n", cx, seat, stop[seat],
            coop_replay_body(seat)[0x2c]);
    check(stop[seat] > cx, "walking left, the seat stops at the canister");
    check((coop_replay_body(seat)[0x2c] & 0x82) == 0x82, "the canister sets its native side contact");
  }
  check(stop[0] == stop[1], "X and the partner stop at the same place");
  puts("MMX CO-OP CANISTER CHECKS PASSED");
}

/* An empty Ride Armor boards whoever touches it, but its idle state
 * ($83:8129) only tested the world body, so Zero as P2 could not get in.
 * From a private co-op checkpoint left of one (MMX_COOP_ARMOR_FIXTURE), each
 * seat in turn walks and jumps right into it while the other stands still. */
static void coop_armor_checks(const char *assets, const char *fixture) {
  coop_replay_start(assets);
  for (unsigned seat = 0; seat < 2; ++seat) {
    check(RtlLoadSnapshot(fixture), "armor fixture loads");
    check(g_ram[0xe18] && !(g_ram[0xe22] & 0x40), "fixture has an empty Ride Armor");
    unsigned other = coop_replay_x(seat ^ 1), boarded = 0;
    for (unsigned t = 0; t < 150 && !boarded; ++t) {
      unsigned in = SNES_PAD_RIGHT | ((t >= 40 && (t % 30) < 12) ? SNES_PAD_B : 0);
      coop_frames(1, seat == 0 ? in : 0, seat == 1 ? in : 0);
      if (g_ram[0xe22] & 0x40) boarded = t + 1;
    }
    coop_frames(1, 0, 0);
    MmxCoopState s = MmxCoopGetState();
    fprintf(stderr, "armor: seat %u boarded at %u, anchor %u, pilot action %02x, other x %u -> %u\n", seat,
            boarded, s.anchor, coop_replay_body(seat)[2], other, coop_replay_x(seat ^ 1));
    check(boarded && coop_replay_body(seat)[2] == 0x2c && s.anchor == seat,
          "the seat that touches the armor boards it and pilots");
    check(coop_replay_body(seat ^ 1)[2] != 0x2c && coop_replay_x(seat ^ 1) == other,
          "the other seat is not boarded or moved");
    for (unsigned t = 0; t < 30; ++t) coop_frames(1, 0, 0); /* boarding animation */
    unsigned ax = g_ram[0xe1d] | g_ram[0xe1e] << 8;
    for (unsigned t = 0; t < 40; ++t) coop_frames(1, seat == 0 ? SNES_PAD_RIGHT : 0, seat == 1 ? SNES_PAD_RIGHT : 0);
    check((g_ram[0xe1d] | g_ram[0xe1e] << 8) > ax, "the pilot drives the armor");
  }
  puts("MMX CO-OP RIDE ARMOR BOARDING CHECKS PASSED");
}

/* The pilot's jump, dash and punch are edge-triggered: the armor ($83:8000)
 * copies the pilot's pressed actions ($0BE2) every frame. With P2 piloting,
 * pilot selection applied P2's input at frame start and the $00:E57F hook
 * then read that frame's actions back as "previous", so nothing was ever
 * pressed and only walking worked. From a private checkpoint with P2 already
 * piloting (MMX_COOP_ARMOR_PILOT_FIXTURE), each action must take effect. */
static void coop_armor_pilot_checks(const char *assets, const char *fixture) {
  coop_replay_start(assets);
  static const struct { const char *name; unsigned pad, armor_state; } moves[] = {
    {"jump", SNES_PAD_B, 0x08}, {"dash", SNES_PAD_A, 0x0e}, {"punch", SNES_PAD_Y, 0x06}};
  for (unsigned m = 0; m < 3; ++m) {
    check(RtlLoadSnapshot(fixture), "armor pilot fixture loads");
    MmxCoopState s = MmxCoopGetState();
    check(s.anchor == 1 && coop_replay_body(1)[2] == 0x2c && (g_ram[0xe22] & 0x40),
          "fixture has P2 piloting the Ride Armor");
    bool seen = false;
    for (unsigned t = 0; t < 20; ++t) {
      coop_frames(1, 0, t < 8 ? moves[m].pad : 0);
      seen |= g_ram[0xe1a] == moves[m].armor_state;
    }
    fprintf(stderr, "armor pilot: P2 %s %s\n", moves[m].name, seen ? "registers" : "is ignored");
    check(seen, "P2's armor action registers");
  }
  puts("MMX CO-OP RIDE ARMOR PILOT CHECKS PASSED");
}

/* X3 Zero's charged shots, landing mid-burst on a slope. The ground phase
 * forced idle straight from the fall, which skipped X1's landing and left
 * the body off the slope; Zero fell again, the burst restarted its phase on
 * every air/ground flip, and he twitched in place for as long as you let him
 * (playtest 2026-10-09, Chill Penguin's stage). From a private co-op
 * checkpoint with Zero as P2 left of a slope (MMX_COOP_ZERO_SLOPE_FIXTURE):
 * walk onto it, charge, jump, release in the air, keep tapping fire. */
static void coop_zero_slope_burst_checks(const char *assets, const char *fixture) {
  coop_replay_start(assets);
  for (int rel = 0; rel < 20; rel += 4) {
    check(RtlLoadSnapshot(fixture), "slope fixture loads");
    for (unsigned t = 0; t < 40; ++t) coop_frames(1, 0, SNES_PAD_RIGHT);
    for (unsigned t = 0; t < 10; ++t) coop_frames(1, 0, 0);
    bool was_ground = true; unsigned flips = 0, release = 150 + (unsigned)rel;
    for (unsigned t = 0; t < 360; ++t) {
      unsigned p2 = 0;
      if (t < release) p2 |= SNES_PAD_Y;
      if (t >= 150 && t < 162) p2 |= SNES_PAD_B;
      if (t > release + 2 && t < release + 90 && ((t - release) % 7) < 2) p2 |= SNES_PAD_Y;
      coop_frames(1, 0, p2);
      bool ground = (coop_replay_body(1)[0x2b] & 4) != 0;
      if (t > 260 && was_ground && !ground) ++flips;
      was_ground = ground;
    }
    MmxZeroState z = MmxCoopGetState().players[1].zero;
    fprintf(stderr, "slope burst +%d: flips %u, burst %u, action %02x\n", rel, flips, z.burst, coop_replay_body(1)[2]);
    check(flips == 0 && !z.burst && coop_replay_body(1)[2] == 0,
          "a burst that lands on a slope finishes and Zero stands");
  }
  puts("MMX CO-OP ZERO SLOPE BURST CHECKS PASSED");
}

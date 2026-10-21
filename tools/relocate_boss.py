#!/usr/bin/env python3
"""Generate an address-only relocation descriptor for an X2/X3 Maverick.

The descriptor tells src/mmx_boss_rush_relocate.c how to rebuild the boss's
original 65816 code inside X1's cart image at activation, from the user's own
X2 or X3 ROM:

* whole source code banks are copied into X1's unused LoROM padding banks
  ($30-$3F canonical; X1's 1.5 MB image is mirrored there and never read);
* the few data tables the boss reads through data bank $86 move into the
  $FF padding at the end of X1's own bank $86, because X1's helpers (damage,
  terrain, hitbox overlap) read hitboxes through that same data bank;
* every traced instruction that names something outside the boss is
  rewritten in place (same length): engine helpers become X1 helpers,
  relocated copies, or small shims; X2/X3 RAM becomes X1 RAM or a zero/sink
  area (reads return zero, ROM writes are ignored).

Anything the descriptor does not classify is an error: unknown helpers or
RAM never silently run X3 addresses inside X1.

Usage: relocate_boss.py <boss> <X2 or X3 USA ROM> <output .inc>
The output contains addresses and the original operand values used to
verify the user's ROM; it contains no graphics, audio or code bytes.
"""
import sys
from snes65816 import Rom, trace

X3_SHA256 = '65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7'

# X3 canonical bank -> X1 canonical bank (all inside X1's $30-$3F padding).
BANKS = {0x3f: 0x3f, 0x13: 0x33, 0x08: 0x38, 0x04: 0x34, 0x02: 0x32}

# Same-engine helpers with identical calling conventions in X1.
X1_HELPERS = {
    0x02d636: 0x8280b4,  # off-screen cull
    0x04c0f7: 0x8491be,  # terrain collision -> +$2B
    0x04be35: 0x849086,  # RNG
    0x04cb74: 0x849b43,  # take player-shot damage (A: 0 none, N dead)
    0x04cb31: 0x849b03,  # contact damage
    0x02d8de: 0x828358,  # allocate an enemy-projectile slot (X, Z set)
    0x02e13f: 0x84aca5,  # face the player
}
# X3 engine routines copied with the boss (object-local or X3 data only).
RELOCATED = {0x04b967, 0x04b94a,          # start / advance animation (X3 sets)
             0x02d7f3, 0x02d78e, 0x02d6f8,  # velocity integration
             0x04cc5c,                    # hitbox overlap with object X
             0x02df64,                    # damage flash timer
             0x04d8d5,                    # stop when the player is dead
             0x13f7d6, 0x13f7fc}          # Rhino's palette rotate / fade back
# X3-only behavior Boss Rush replaces. Values name shim bodies below.
SHIMS = {
    0x0084bc: 'rtl',     # music request (Rush owns the battle song)
    0x00850f: 'rtl',     # SPC bank upload (boss sounds come later)
    0x00b1dc: 'rtl',     # Doppler refight graphics
    0x018000: 'sfxpan',  # positional sound effect (hooked: cached boss clip)
    0x01802b: 'sfx',     # sound effect (hooked: cached boss clip)
    0x04d1ca: 'lock',    # lock player (X1 $84:9FEB; Rush skips it during entrances)
    0x04d1ef: 'release', # release a locked player (X1 $84:A003)
    0x04d580: 'rtl',     # screen shake
    0x02df8a: 'rtl',     # dust effect
    0x02e0ba: 'rtl',     # stock boss explosion (Rush draws its own)
    0x02e0e6: 'rtl',
    0x04d8cf: 'rtl',     # weapon award checks (never reached in Rush)
    0x04d8e1: 'rtl',
    0x04d6e0: 'rtl',     # projectile death sparkle
    0x02ddfe: 'rtl',     # weapon stun reaction
    0x02e016: 'clc',     # palette fade step: report finished
    0x02db3f: 'combat',  # generic hurt reaction: resume combat
    0x02d928: 'delete',  # delete this object
    0x02d933: 'delete',
    0x02e15c: 'init',    # class init row (generated from the ROM)
}
# X3 RAM -> X1 RAM. The $09xx/$0Axx player block is X1's $0Bxx/$0Cxx + $1D0.
RAM = {0x09cb: 0x0b9b, 0x09cc: 0x0b9c, 0x09da: 0x0baa, 0x09dd: 0x0bad, 0x09e0: 0x0bb0,
       0x09ff: 0x0bcf, 0x0a03: 0x0bd3, 0x1e5d: 0x1e4d, 0x1e60: 0x1e50, 0x1f1f: 0x1f0e,
       0x1f2f: 0x1f1d,  # last hit's (reaction) class, written by the damage helper
       # Boss profile ($28) the flash timer restores; unused in X1 and swapped
       # per Rush seat, so two relocated bosses never share it.
       0x1f57: 0x1fd5}
# X3-only globals: reads see zero, writes land in ROM and are ignored.
ZERO = {0x0040, 0x00a1, 0x0a43, 0x1f18, 0x1f36, 0x1f54, 0x1f55, 0x1f56,
        0x1fae, 0x1fca, 0x1fda, 0x1fe0, 0x049e, 0x0480, 0x0482, 0x0484, 0x04a0}
ZERO_LONG = {0x001f39, 0x7fcac0}
# Player states compared after LDA $09DA (X1 $0BAA). Hurt $0E, death $0C
# and the scripted lock $1E match X1. X3's caught state $12 has no X1
# counterpart; a drill hit leaves X1's player hurt ($0E) instead. $74 is
# unidentified in both games and kept.
PLAYER_STATES = {0x0c: 0x0c, 0x0e: 0x0e, 0x12: 0x0e, 0x1e: 0x1e, 0x74: 0x74}
# 16-bit immediates that are RAM object pointers.
IMMEDIATE = {0x09d8: 0x0ba8}

# X1 bank $86 padding ($FA44-$FFFF is $FF fill in the USA ROM). $FF40-$FFFF
# holds the saber, weapon and Zero collision tables, so the zero area ends
# below it. The zero area only ever reads as zero (ROM writes are ignored),
# so globals that share its low byte may share a slot.
PAD_SHIMS, PAD_DATA, PAD_ZERO = 0x86fa80, 0x86fb00, 0x86fe40
SLOT_TABLES = 0x3c8000  # 256 bytes per source slot table
ZERO_MASK = 0xff


class Game:
    """One source game's engine as relocated bosses see it."""
    def __init__(self, number, sha256, banks, helpers, relocated, shims, ram, zero,
                 zero_long, player_states, immediate, init_fields, sound_upload, sfx,
                 long_keep=(), data_banks=(), anim_directory=None, slot_tables=None):
        self.number, self.sha256, self.banks = number, sha256, banks
        self.helpers, self.relocated, self.shims = helpers, relocated, shims
        self.ram, self.zero, self.zero_long = ram, zero, zero_long
        self.player_states, self.immediate = player_states, immediate
        # init_fields(rom, x_class) -> {field: (source address or None, constant)}
        self.init_fields = init_fields
        self.sound_upload, self.sfx = sound_upload, sfx  # helper addresses
        self.long_keep = set(long_keep)  # long RAM addresses used as-is
        # Banks copied for their data only (e.g. X2's animation bank $2F).
        self.data_banks = set(data_banks)
        # 3-byte entries (pointer, bank) per sprite set; the bank byte of each
        # set the boss uses is rewritten to the relocated copy of that bank.
        self.anim_directory = anim_directory
        # Per-stage tables indexed by graphics slot (long RAM base -> {slot:
        # value}), read directly ($7F:8345) or indexed ($7F:8300,x). Each is
        # copied whole (256 slots, 0 where the room loads nothing, as the
        # stage loader clears them) to SLOT_TABLES in unused padding bank $3C.
        self.slot_tables = dict(slot_tables or {})


def x3_init_fields(rom, x_class):
    row = 0x86e28e + (x_class - 1) * 5
    return {0x16: (row, 0), 0x26: (row + 2, 0), 0x27: (row + 3, 0), 0x28: (row + 4, 0)}


X3 = Game(3, X3_SHA256, BANKS, X1_HELPERS, RELOCATED, SHIMS, RAM, ZERO, ZERO_LONG,
          PLAYER_STATES, IMMEDIATE, x3_init_fields, 0x00850f, (0x01802b, 0x018000))


class Boss:
    def __init__(self, name, roster_id, body, children, x3_class, data, profiles, flinch,
                 game=None, pad=(PAD_SHIMS, PAD_DATA, PAD_ZERO), sets=(), effects=None):
        self.name, self.roster_id, self.body = name, roster_id, body
        self.game = game or X3
        # $86 padding: shims from pad[0], data tables from pad[1], below pad[2].
        self.pad = pad
        self.sets = sets            # sprite sets whose animation bank is fixed up
        self.effects = effects or {}  # effect-list children: class -> controller
        self.children, self.x3_class, self.data = children, x3_class, data
        # X3 damage profile (+$28) -> X1 row at $86:EF37. X1 rows 6-13 are the
        # Mavericks; row 0 deflects every shot, row 5 takes no damage.
        self.profiles = profiles
        # X3 weakness shot classes compared against $1F2F -> X1 reaction
        # classes the Rush weakness hook reports for this boss's weaknesses.
        self.flinch = flinch


RHINO = Boss('rhino', 23, 0x3fe5e4, {0x26: 0x08ab9b}, 0x55,
             # $86 tables read through the data bank: hitboxes, drill spawn
             # tables (through the spread offsets at $EFE7 + 4*index, index
             # 0-3, ending at $EFF6) and the shared angle table ($00-$70).
             [(0x86ec87, 10), (0x86efb8, 0x3f), (0x86e18e, 0x74)],
             # Normal: Kuwanger's row (its Homing Torpedo column carries the
             # weakness; Rush neutralizes Torpedo itself for Rhino). Drill
             # charge deflects ($0D -> row 0); the flinch is invulnerable.
             {0x18: 0x0a, 0x0d: 0x00, 0x01: 0x05},
             # Acid Burst parts $07/$10/$18/$21 -> X1 Torpedo normal/charged.
             {0x07: 0x0e, 0x10: 0x17, 0x18: 0x0e, 0x21: 0x17})


# --- Mega Man X2 -------------------------------------------------------------
# X2's engine sits between X1 and X3: its object layout, RAM and helper
# calling conventions match X3 (player $09D8, enemy shots from $10D8, $1F1D),
# while many helpers are byte-for-byte X1 routines.
X2_SHA256 = 'f3246755f608a1e1dc9c848b61da3b824c7853b29b3be40df6fc7f2793a887ed'
# X2 canonical bank -> X1 padding bank (Rhino owns $3F/$33/$38/$34/$32).
X2_BANKS = {0x04: 0x35, 0x03: 0x36, 0x08: 0x37, 0x00: 0x39, 0x2f: 0x3a, 0x01: 0x3b}
X2_HELPERS = {
    0x00d859: 0x8280b4,  # off-screen cull (same code as X1)
    0x00d919: 0x828174,  # velocity / position helpers (same code as X1)
    0x00d93a: 0x828195,
    0x00d98d: 0x8281e8,
    0x00d9af: 0x82820a,
    0x00db5c: 0x828398,  # delete this object (same code as X1)
    0x00db1c: 0x828358,  # allocate an enemy-shot slot ($1318 list = X1 $1428)
    0x00da97: 0x8282d3,  # allocate an effect slot ($1818 list = X1 $1928)
    0x08cb42: 0x849086,  # RNG
    0x08cd5e: 0x8491be,  # terrain collision -> +$2B
    0x08d79a: 0x849b43,  # take player-shot damage (same contract as X3)
    0x08d752: 0x849b03,  # contact damage
    0x00d813: 0x82806e,  # same code as X1
    0x00db67: 0x8283a3,  # delete variant (same code as X1)
    0x0481c1: 0x84acbe,  # angle/velocity math (X1's copy uses X1's sine table)
    0x08dd6a: 0x84a081,  # same code as X1
    0x08cb5c: 0x8490a0,  # same code as X1
}
X2_RELOCATED = {0x08c673, 0x08c690,      # advance / start animation (X2 sets in $2F)
                0x08e2df,                # refight test (stage $1FAD == $0C)
                0x08e2e5}                # stop when the player is dead
X2_SHIMS = {
    0x008457: 'rtl',      # music request (Rush owns the battle song)
    0x00849d: 'rtl',      # SPC bank upload (boss sounds come from the cache)
    0x00851e: 'sfx',      # sound effect (hooked: cached boss clip)
    0x00ad18: 'rtl',      # refight-only scene setup
    0x08dcdd: 'lock',     # lock player for the entrance
    0x08dd02: 'release',  # release the player
    0x08e2f1: 'rtl',      # weapon award / refight bookkeeping (never in Rush)
    0x2abfcb: 'stockdeath',  # stock death scene (Rush draws its own explosion)
    0x00da22: 'init',     # class init
    0x08e0f0: 'rtl',      # shot explosion effect + sound
}
X2_RAM = dict(RAM)
X2_RAM.update({0x1f2d: 0x1f1b,   # last hit's direction (X1 $84:9EB7)
               0x1f1d: 0x1f0c})  # scene flag: enemies cannot hurt the player
# X2-only globals: refight/stage checks, weapon ownership, and the screen
# darkening of his lightning ($00C9-$00CD color math, $0962-$0968, $08DF).
X2_ZERO = set(ZERO) | {0x1fad, 0x1fc5, 0x1fd9, 0x00c9, 0x00ca, 0x00cb, 0x00cc,
                       0x00cd, 0x08df, 0x0a0d} | set(range(0x0962, 0x0969))
X2_ZERO_LONG = set(ZERO_LONG)
# The stage's sprite slot tables as his room loads them ($2A:B2D4 stage 12):
# slot $45 (body) at VRAM $1000, slot $46 (vine, lightning) at $0480, both
# with palette list $9E from OBJ palette 4. $7F:8200 holds a slot's tile
# base (+$18), $7F:8300 its attributes (+$11: priority 2, palette 4, and
# bit 0 for VRAM $1000's upper 256 tiles). The renderer draws each object
# from its base and palette as X2's sprite builder does.
X2_SLOT_TABLES = {0x7f8200: {0x45: 0x00, 0x46: 0x48}, 0x7f8300: {0x45: 0x29, 0x46: 0x28}}


def x2_init_fields(rom, x_class):
    # $00:DA22: $01 = 2, profile 3, sprite set from $86:A34D + 2*(class-1).
    # It also copies the class's graphics slot attributes from $7F:8300 into
    # +$11; Rush has no X2 slot table, so use OBJ priority 2 and palette 4
    # (as Rhino's own init does). Palette 0 reads as a hit flash and
    # priority 0 hides the body behind the arena.
    return {0x16: (0x86a34d + (x_class - 1) * 2, 0), 0x28: (None, 3), 0x11: (None, 0x29)}


# Body/children scratch at $7F:CD90-$7F:CD96: Rush swaps a private copy per
# seat into it while the boss's objects run (MmxBossRushForeignSetScratch).
X2_LONG_KEEP = set(range(0x7fcd90, 0x7fcd97))

X2 = Game(2, X2_SHA256, X2_BANKS, X2_HELPERS, X2_RELOCATED, X2_SHIMS, X2_RAM, X2_ZERO,
          X2_ZERO_LONG, PLAYER_STATES, IMMEDIATE, x2_init_fields, 0x00849d, (0x00851e,),
          X2_LONG_KEEP, data_banks={0x2f}, anim_directory=0x2fa000, slot_tables=X2_SLOT_TABLES)

SPONGE = Boss('sponge', 13, 0x049a29, {0x10: 0x03d5c9}, 0x23,
              # $86 tables: his hitboxes ($C989-$C9A6), attack choice,
              # velocity and bolt offsets and the lightning color block
              # (to $C9E5); his children's hitboxes ($DF2E-$DF4B) through
              # the whip tip's 16 angle entries of 8 bytes (to $DFFD); the
              # whip segments' angle entries ($EA59-$EADE).
              # His halves (debris class $2B, variants $52/$53) read 4-byte
              # entries of the table at $EB38 and its random velocities
              # ($EE38/$EE48).
              [(0x86c989, 0x5d), (0x86df2e, 0xd0), (0x86ea59, 0x86),
               (0x86eb38 + 0x52 * 4, 8, 0x86eb38), (0x86ee38, 0x20)],
              # X1 row 8 (Penguin's): weak to class $0B/$14, X2's own Sonic
              # Slicer classes. Row 5 takes no damage (his hit flash).
              # Children: $03 damageable (X1 row 3), $0A deflects all but
              # Sonic Slicer (X1 row 0, deflect).
              {0x03: 0x08, 0x09: 0x08, 0x05: 0x05, 0x0a: 0x00},
              {0x0b: 0x0b, 0x14: 0x14},
              game=X2, pad=(0x86fbc0, 0x86fc50, PAD_ZERO), sets=(0x3e,),
              # Effects: whip segments ($1F) and the two halves of his split
              # death ($2B, variants $52/$53).
              effects={0x1f: 0x03ecbe, 0x2b: 0x01f068})


def bank_map(address, banks=BANKS):
    canonical = (address >> 16) & 0x7f
    if canonical not in banks:
        return None
    return (address & 0x80ffff) | (banks[canonical] << 16)


def shim_code(kind, init, extra=()):
    if kind in ('rtl', 'sfx', 'sfxpan'):
        return [0x6b]  # sfx shims get their own addresses for the runtime hook
    if kind == 'clc':
        return [0x18, 0x6b]
    if kind == 'stockdeath':
        # X2 $2A:BFCB, the stock boss death scene, keeps only its timing
        # contract: the first call waits 60 frames ($34), then sets $02 = 2
        # (a split death continues); a later call ($02 != 0) finishes ($33 =
        # $FF). Rush plays the music, flashes and explosion itself.
        # PHP; SEP #$20; LDA $02; BNE later; LDA $03; BNE wait;
        # LDA #$3C; STA $34; LDA #$02; STA $03; PLP; RTL;
        # wait: DEC $34; BNE done; LDA #$02; STA $02; STZ $03; done: PLP; RTL;
        # later: LDA #$FF; STA $33; PLP; RTL
        return [0x08, 0xe2, 0x20, 0xa5, 0x02, 0xd0, 0x1a, 0xa5, 0x03, 0xd0, 0x0a,
                0xa9, 0x3c, 0x85, 0x34, 0xa9, 0x02, 0x85, 0x03, 0x28, 0x6b,
                0xc6, 0x34, 0xd0, 0x06, 0xa9, 0x02, 0x85, 0x02, 0x64, 0x03, 0x28, 0x6b,
                0xa9, 0xff, 0x85, 0x33, 0x28, 0x6b]
    if kind == 'combat':
        return [0x08, 0xe2, 0x20, 0xa9, 0x02, 0x85, 0x01, 0x28, 0x6b]
    if kind == 'lock':
        # X3 $04:D1CA locks a living player into state $1E; X1's entrance
        # lock sets the same state. PHP; SEP #$20; LDA $0BCF; AND #$7F;
        # BEQ +4; JSL $84:9FEB; PLP; RTL
        return [0x08, 0xe2, 0x20, 0xad, 0xcf, 0x0b, 0x29, 0x7f, 0xf0, 0x04,
                0x22, 0xeb, 0x9f, 0x84, 0x28, 0x6b]
    if kind == 'release':
        # X3 $04:D1EF releases only a player still locked in $1E. A lock
        # taken while the player was hurt skipped the end of X1's hurt state,
        # which starts the 60-frame countdown ($0C13) that clears the hurt
        # latches $0BD8/$0C32; start it here, or the player stays
        # invulnerable. PHP; SEP #$20; LDA $0BAA; CMP #$1E; BNE done;
        # JSL $84:A003; LDA $0BD8; BEQ done; LDA $0C13; BNE done;
        # LDA #$3C; STA $0C13; done: PLP; RTL
        return [0x08, 0xe2, 0x20, 0xad, 0xaa, 0x0b, 0xc9, 0x1e, 0xd0, 0x13,
                0x22, 0x03, 0xa0, 0x84, 0xad, 0xd8, 0x0b, 0xf0, 0x0a,
                0xad, 0x13, 0x0c, 0xd0, 0x05, 0xa9, 0x3c, 0x8d, 0x13, 0x0c,
                0x28, 0x6b]
    if kind == 'delete':
        # X3 $02:D928 (= X1 $82:8398): clear $00-$03 and $0E as words. X1's
        # slot allocators test the first word, so a lone 8-bit STZ $00
        # leaves dead objects holding their slots. REP #$20; STZ $00;
        # STZ $02; STZ $0E; SEP #$20; RTL
        return [0xc2, 0x20, 0x64, 0x00, 0x64, 0x02, 0x64, 0x0e, 0xe2, 0x20, 0x6b]
    if kind == 'init':
        # PHP; SEP #$30; clear $30/$1A-$1F/$18/$10; $01=2; $26/$27/$28/$16
        # from the class row (filled by the builder); PLP; RTL.
        code = [0x08, 0xe2, 0x30, 0x64, 0x30]
        for f in (0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x18, 0x10):
            code += [0x64, f]
        code += [0xa9, 0x02, 0x85, 0x01]
        for field in (0x26, 0x27, 0x28, 0x16) + tuple(extra):
            init.append((len(code) + 1, field))
            code += [0xa9, 0x00, 0x85, field]
        return code + [0x28, 0x6b]
    raise ValueError(kind)


def build(rom, boss):
    game = boss.game
    X1_HELPERS, RELOCATED, SHIMS = game.helpers, game.relocated, game.shims
    RAM, ZERO, ZERO_LONG = game.ram, game.zero, game.zero_long
    PLAYER_STATES, IMMEDIATE, BANKS = game.player_states, game.immediate, game.banks
    PAD_SHIMS, PAD_DATA, PAD_END = boss.pad
    bank_map_ = lambda address: bank_map(address, BANKS)
    stop = lambda t: (t & 0x7fffff) in X1_HELPERS or (t & 0x7fffff) in SHIMS
    entries = [boss.body, *boss.children.values(), *boss.effects.values(), *RELOCATED]
    seen, tables, calls = trace(rom, entries, stop=stop)
    errors, patches, copies, blobs = [], [], [], []

    # Data tables into X1's $86 padding, keeping each table's layout.
    data_map, cursor, bases = [], PAD_DATA, {}
    for entry in boss.data:
        start, length = entry[0], entry[1]
        data_map.append((start, length, cursor))
        copies.append((start, cursor, length))
        if len(entry) > 2:
            # Only this window of a table based at entry[2] is used: the base
            # pointer moves so base + index lands on the copy.
            bases[entry[2] & 0xffff] = ((cursor & 0xffff) - ((start - entry[2]) & 0xffff)) & 0xffff
        cursor += length
    slot_at = {}  # table base -> copy
    for n, (base, values) in enumerate(sorted(game.slot_tables.items())):
        slot_at[base] = SLOT_TABLES + n * 0x100
        blobs.append((slot_at[base], [values.get(slot, 0) for slot in range(0x100)]))

    def slot_target(address, mode):
        for base, copy in slot_at.items():
            if base <= address < base + 0x100 and (mode == 'long' or address == base):
                return copy + address - base
        return None
    if cursor > PAD_END:
        errors.append('data tables overflow the $86 padding')

    def data_target(offset):
        for start, length, dest in data_map:
            if start & 0xffff <= offset < (start & 0xffff) + length:
                return (dest & 0xffff) + offset - (start & 0xffff)
        return None

    # Shims, one body per kind.
    shim_at, cursor, init_fields = {}, PAD_SHIMS, []
    for kind in sorted(set(SHIMS.values())):
        fields = []
        # Fields beyond X3's class row that a game's init also sets.
        extra = sorted(set(game.init_fields(rom, boss.x3_class)) - {0x16, 0x26, 0x27, 0x28})
        code = shim_code(kind, fields, extra)
        shim_at[kind] = cursor
        blobs.append((cursor, code))
        if kind == 'init':
            sources = game.init_fields(rom, boss.x3_class)
            # The damage profile becomes the boss's X1 row, written directly;
            # constants are written into the shim; the rest copy from the ROM.
            for at, field in fields:
                address, constant = sources.get(field, (None, 0))
                if field == 0x28:
                    profile = rom.byte(address) if address else constant
                    if profile not in boss.profiles:
                        errors.append(f'class row profile ${profile:02X} has no X1 row')
                    code[at] = boss.profiles.get(profile, 0)
                elif address is None:
                    code[at] = constant
            init_fields = [(cursor + at, sources[field][0]) for at, field in fields
                           if field != 0x28 and sources.get(field, (None, 0))[0] is not None]
        cursor += len(code)
    if cursor > PAD_DATA:
        errors.append('shims overflow their padding')

    def call_target(target):
        t = target & 0x7fffff
        if t in X1_HELPERS:
            return X1_HELPERS[t]
        if t in SHIMS:
            return shim_at[SHIMS[t]]
        if t in RELOCATED or any(t == (e & 0x7fffff) for e in (boss.body, *boss.children.values(), *boss.effects.values())):
            return bank_map_(target)
        mapped = bank_map_(target)
        if mapped is not None and target in seen:
            return mapped
        return None

    for a, insn in sorted(seen.items()):
        if bank_map_(a) is None:
            errors.append(f'${a:06X}: traced code outside the copied banks')
            continue
        dest = bank_map_(a)
        operand = dest + 1 if (dest & 0xffff) != 0xffff else None
        v, md, name = insn.value, insn.mode, insn.name

        def patch(size, value):
            patches.append((operand, size, v, value))

        if name in ('JSL', 'JML') and md == 'long':
            target = call_target(v)
            if target is None:
                errors.append(f'${a:06X}: unclassified call ${v:06X}')
            elif target != v:
                patch(3, target)
        elif md in ('long', 'long,x'):
            if v in game.long_keep:
                pass
            elif slot_target(v, md) is not None:
                patch(3, slot_target(v, md))
            elif v in ZERO_LONG:
                patch(3, PAD_ZERO + (v & ZERO_MASK))
            elif bank_map_(v) is not None:
                if bank_map_(v) != v:
                    patch(3, bank_map_(v))
            else:
                errors.append(f'${a:06X}: unclassified long ${v:06X}')
        elif md in ('abs', 'abs,x', 'abs,y') and name not in ('JSR', 'JMP', 'PEA'):
            if v >= 0x8000:
                moved = data_target(v)
                if moved is None:
                    errors.append(f'${a:06X}: data read ${v:04X} outside relocated tables')
                elif moved != v:
                    patch(2, moved)
            elif md != 'abs' and v < 0x40:
                pass  # object field through X/Y
            elif 0x2100 <= v < 0x2200 or 0x4200 <= v < 0x4400:
                pass  # SNES hardware registers (multiply/divide)
            elif md == 'abs' and v < 0x10:
                pass  # direct-page-style scratch shared by both engines
            elif v in RAM:
                patch(2, RAM[v])
                if v == 0x09da:
                    # Player-state test: CMP #state right after the load.
                    at = (a & 0xff0000) | ((a + insn.size) & 0xffff)
                    cmp = seen.get(at)
                    if cmp and cmp.name == 'CMP' and cmp.size == 2:
                        if cmp.value not in PLAYER_STATES:
                            errors.append(f'${at:06X}: player state ${cmp.value:02X} unmapped')
                        elif PLAYER_STATES[cmp.value] != cmp.value:
                            patches.append((bank_map_(at) + 1, 1, cmp.value, PLAYER_STATES[cmp.value]))
                if v == 0x1f2f:
                    # Weakness test: the CMP chain after this load names the
                    # X3 weakness shot classes; use X1 reaction classes.
                    at = (a & 0xff0000) | ((a + insn.size) & 0xffff)
                    while at in seen and seen[at].name in ('CMP', 'BEQ', 'BNE'):
                        cmp = seen[at]
                        if cmp.name == 'CMP':
                            if cmp.value not in boss.flinch:
                                errors.append(f'${at:06X}: weakness class ${cmp.value:02X} unmapped')
                            else:
                                patches.append((bank_map_(at) + 1, 1, cmp.value, boss.flinch[cmp.value]))
                        at = (at & 0xff0000) | ((at + cmp.size) & 0xffff)
            elif v in ZERO:
                patch(2, (PAD_ZERO & 0xffff) + (v & ZERO_MASK))
            else:
                errors.append(f'${a:06X}: unclassified RAM ${v:04X} ({name})')
        elif name in ('MVN', 'MVP'):
            # Block move: LDX #source; LDY #dest; LDA #count; MVN dest,source.
            # A destination in X2/X3-only RAM is redirected into the zero/sink
            # area (ROM writes are ignored); a source table moves with the data.
            dst_bank, src_bank = v & 0xff, v >> 8
            setup = {}
            at = a
            for _ in range(4):
                prev = next((i for i in seen.values() if (i.address & 0xff0000) == (at & 0xff0000)
                             and ((i.address + i.size) & 0xffff) == (at & 0xffff)), None)
                if prev is None:
                    break
                if prev.name in ('LDX', 'LDY') and prev.size == 3:
                    setup.setdefault(prev.name, prev)
                at = prev.address
            ldx, ldy = setup.get('LDX'), setup.get('LDY')
            if ldx is None or ldy is None:
                errors.append(f'${a:06X}: block move without immediate source/destination')
            else:
                if dst_bank in (0x00, 0x7e) and ldy.value in ZERO:
                    patches.append((bank_map_(ldy.address) + 1, 2, ldy.value,
                                    (PAD_ZERO & 0xffff) + (ldy.value & ZERO_MASK)))
                    patches.append((operand, 1, dst_bank, PAD_ZERO >> 16))
                elif dst_bank in (0x00, 0x7e) and ldy.value not in RAM:
                    errors.append(f'${a:06X}: block move into unclassified RAM ${ldy.value:04X}')
                moved = data_target(ldx.value)
                if (src_bank & 0x7f) == 0x06:
                    if moved is None:
                        errors.append(f'${a:06X}: block move from ${ldx.value:04X} outside relocated tables')
                    elif moved != ldx.value:
                        patches.append((bank_map_(ldx.address) + 1, 2, ldx.value, moved))
                        patches.append((dest + 2, 1, src_bank, 0x86))
        elif md in ('immm', 'immx') and insn.size == 3 and v in IMMEDIATE:
            patch(2, IMMEDIATE[v])
        elif md in ('immm', 'immx') and insn.size == 3 and v in bases:
            patch(2, bases[v])
        elif md in ('immm', 'immx') and insn.size == 3:
            moved = data_target(v)
            if moved is not None and moved != v and 0x8000 <= v:
                patch(2, moved)  # hitbox pointer immediates
            elif moved is None and 0x8000 <= v and name == 'LDA':
                # A 16-bit pointer stored into an object's hitbox field (+$20)
                # must name a relocated $86 table.
                nxt = seen.get((a & 0xff0000) | ((a + 3) & 0xffff))
                if nxt and nxt.name == 'STA' and nxt.value == 0x20 and nxt.mode in ('dp', 'abs,x'):
                    errors.append(f'${a:06X}: hitbox pointer ${v:04X} outside relocated tables')
        elif md in ('immm', 'imm8') and insn.size == 2 and name == 'LDA':
            nxt = seen.get((a & 0xff0000) | ((a + 2) & 0xffff))
            if nxt and nxt.name == 'PHA' and v in BANKS and BANKS[v] != v:
                patch(1, BANKS[v])
            if nxt and nxt.name == 'STA' and nxt.mode == 'dp' and nxt.value == 0x28:
                # Damage profile switch: X3 row -> X1 row.
                if v not in boss.profiles:
                    errors.append(f'${a:06X}: X3 damage profile ${v:02X} has no X1 row')
                elif boss.profiles[v] != v:
                    patch(1, boss.profiles[v])
            if nxt and nxt.name == 'STA' and nxt.mode == 'dp' and nxt.value == 0x20:
                # Split hitbox pointer: LDA #lo; STA $20; LDA #hi; STA $21.
                hi = seen.get((a & 0xff0000) | ((a + 4) & 0xffff))
                if hi and hi.name == 'LDA' and hi.size == 2:
                    moved = data_target(hi.value << 8 | v)
                    if moved is None:
                        errors.append(f'${a:06X}: split pointer ${hi.value:02X}{v:02X} not relocated')
                    elif moved != (hi.value << 8 | v):
                        patches.append((operand, 1, v, moved & 0xff))
                        patches.append((bank_map_(hi.address) + 1, 1, hi.value, moved >> 8))

    # Damage profile loads that reach STA $28 through instructions that keep
    # A (e.g. LDA #$0A; STA $3B; STA $28), or both arms of a conditional
    # load (LDA #9; LDX $37; BEQ +2; LDA #5; STA $28).
    keeps_a = {'STA', 'STX', 'STY', 'STZ', 'LDX', 'LDY', 'CPX', 'CPY', 'INX', 'INY',
               'DEX', 'DEY', 'BEQ', 'BNE', 'BMI', 'BPL', 'BCC', 'BCS', 'BVC', 'BVS',
               'SEP', 'REP', 'CLC', 'SEC', 'NOP', 'PHA', 'PHX', 'PHY'}
    branches = {'BEQ', 'BNE', 'BMI', 'BPL', 'BCC', 'BCS', 'BVC', 'BVS'}
    ordered = sorted(seen.items())
    index = {a: i for i, (a, _) in enumerate(ordered)}
    for a, insn in ordered:
        if insn.name != 'STA' or insn.mode != 'dp' or insn.value != 0x28:
            continue
        i, found = index[a] - 1, 0
        while i >= 0 and index[a] - i <= 6 and found < 2:
            pa, prev = ordered[i]
            if prev.name == 'LDA' and prev.mode == 'immm' and prev.size == 2:
                if prev.value not in boss.profiles:
                    errors.append(f'${pa:06X}: damage profile ${prev.value:02X} has no X1 row')
                elif boss.profiles[prev.value] != prev.value:
                    patches.append((bank_map_(pa) + 1, 1, prev.value, boss.profiles[prev.value]))
                found += 1
                if i == 0 or ordered[i - 1][1].name not in branches:
                    break
            elif prev.name not in keeps_a:
                break
            i -= 1

    # Sound: the boss's SPC bank (LDA #bank; JSL $00:850F) and every effect
    # it requests (LDA #effect; JSL $01:802B or $01:8000).
    sound_bank, effects, prev = None, set(), None
    for a, insn in sorted(seen.items()):
        if insn.name == 'JSL' and prev and prev.name == 'LDA' and prev.size == 2:
            if insn.value & 0x7fffff == game.sound_upload:
                sound_bank = prev.value
            if insn.value & 0x7fffff in game.sfx:
                effects.add(prev.value)
        prev = insn
    if sound_bank is None:
        errors.append('no SPC sound bank upload found')
    if errors:
        raise SystemExit('Relocation failed:\n  ' + '\n  '.join(errors))
    if game.anim_directory is not None:
        for sprite_set in boss.sets:
            entry = game.anim_directory + sprite_set * 3 + 2
            source = rom.byte(entry)
            if source not in BANKS:
                errors.append(f'sprite set ${sprite_set:02X} animates from unmapped bank ${source:02X}')
            else:
                patches.append((bank_map_(entry), 1, source, BANKS[source]))
    banks = sorted({(a >> 16) & 0x7f for a in seen} | game.data_banks)
    sound = (sound_bank, sorted(effects), shim_at['sfx'], shim_at.get('sfxpan', shim_at['sfx']))
    return banks, patches, copies, blobs, init_fields, len(seen), sound


def emit(path, boss, banks, patches, copies, blobs, init_fields, count, sound):
    out = [f'/* Generated by tools/relocate_boss.py ({boss.name}); do not edit.',
           ' * Addresses and original operand values only: the builder copies the',
           f" * code and data from the user's X{boss.game.number} ROM at activation. */",
           f'static const MmxRelocBank k_{boss.name}_banks[]={{']
    out += [f'  {{0x{b:02x},0x{boss.game.banks[b]:02x}}},' for b in banks]
    out += ['};', f'static const MmxRelocCopy k_{boss.name}_copies[]={{']
    out += [f'  {{0x{s:06x},0x{d:06x},{n}}},' for s, d, n in copies]
    out += [f'  {{0x{s:06x},0x{d:06x},1}},' for d, s in init_fields]
    out += ['};', f'static const uint8_t k_{boss.name}_shim_code[]={{']
    for at, code in blobs:
        out.append(f'  /* ${at:06X} */ ' + ','.join(f'0x{b:02x}' for b in code) + ',')
    out += ['};', f'static const MmxRelocBlob k_{boss.name}_blobs[]={{']
    offset = 0
    for at, code in blobs:
        out.append(f'  {{0x{at:06x},{offset},{len(code)}}},')
        offset += len(code)
    out += ['};', f'static const MmxRelocPatch k_{boss.name}_patches[]={{']
    out += [f'  {{0x{a:06x},{n},0x{old:06x},0x{new:06x}}},' for a, n, old, new in sorted(set(patches))]
    out += ['};',
            f'/* {count} traced instructions, {len(set(patches))} rewritten operands. */',
            f'enum {{ k_{boss.name}_roster_id={boss.roster_id},'
            f' k_{boss.name}_body=0x{bank_map(boss.body, boss.game.banks) | 0x800000:06x} }};']
    out += [f'static const MmxRelocChild k_{boss.name}_children[]={{']
    out += [f'  {{0x{c:02x},0x{bank_map(a, boss.game.banks) | 0x800000:06x}}},' for c, a in boss.children.items()]
    out += ['};', f'/* Effect-list children ($1928 list): class -> controller. */',
            f'enum {{ k_{boss.name}_effect_child_count={len(boss.effects)} }};',
            f'static const MmxRelocChild k_{boss.name}_effect_children[]={{']
    out += [f'  {{0x{c:02x},0x{bank_map(a, boss.game.banks) | 0x800000:06x}}},' for c, a in boss.effects.items()]
    if not boss.effects:
        out.append('  {0,0}, /* none */')
    bank, effects, sfx, sfx_pan = sound
    out += ['};',
            f'enum {{ k_{boss.name}_sound_bank=0x{bank:02x}, k_{boss.name}_sfx=0x{sfx:06x},'
            f' k_{boss.name}_sfx_pan=0x{sfx_pan:06x} }};',
            f'static const uint8_t k_{boss.name}_effects[]={{' + ','.join(f'0x{e:02x}' for e in effects) + '};',
            '']
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(out))


BOSSES = {boss.name: boss for boss in (RHINO, SPONGE)}


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in BOSSES:
        raise SystemExit(__doc__ + 'Bosses: ' + ', '.join(sorted(BOSSES)))
    boss = BOSSES[sys.argv[1]]
    rom = Rom(sys.argv[2], boss.game.sha256)
    result = build(rom, boss)
    emit(sys.argv[3], boss, *result)
    print(f'{boss.name}: {result[5]} instructions, {len(set(result[1]))} patches -> {sys.argv[3]}')


if __name__ == '__main__':
    main()

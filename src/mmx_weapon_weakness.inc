/* Explicit X1 targets and source attacks. Damage references the live native
 * row; reaction classes are separate so a weakness need not change element.
 * X1 normal classes $07..0E have charged counterparts nine entries later. */
enum {
  X1_ICE=0x07, X1_SHIELD=0x08, X1_TORNADO=0x09, X1_FIRE=0x0a,
  X1_STING=0x0b, X1_SPARK=0x0c, X1_CUTTER=0x0d, X1_TORPEDO=0x0e,
  KEEP_ATTACK_CLASS=0xff
};
enum {
  X2_BUBBLE=2, X2_WHEEL=4, X2_SLICER=5, X2_CHAIN=6, X2_SPEED=8,
  X3_BOMB=2, X3_TRIAD=3, X3_BLADE=4, X3_FROST=7
};
typedef struct BossWeakness {
  uint8_t boss, page, weapon;
  uint8_t damage[2], reaction[2]; /* normal, charged */
} BossWeakness;
static const BossWeakness boss_weaknesses[] = {
  {0x02,1,X2_SPEED,{X1_FIRE,X1_FIRE+9},{X1_FIRE,X1_FIRE+9}}, /* Penguin: Speed Burner */
  {0x31,1,X2_CHAIN,{X1_ICE,X1_ICE+9},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Mandrill: Strike Chain */
  {0x31,2,X3_FROST,{X1_ICE,X1_ICE+9},{X1_ICE,X1_ICE+9}}, /* Mandrill: Frost Shield */
  {0x14,2,X3_TRIAD,{X1_SPARK,X1_SPARK+9},{X1_SPARK,X1_SPARK+9}}, /* Armadillo: Triad Thunder */
  /* Charged Rolling Shield is a defensive barrier with zero boss damage.
   * Damaging electric attacks use its normal weakness value in both forms. */
  {0x07,2,X3_TRIAD,{X1_SHIELD,X1_SHIELD},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Octopus: Triad Thunder */
  {0x07,0,6,{X1_SHIELD,X1_SHIELD},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Octopus: Electric Spark */
  {0x05,2,X3_BOMB,{X1_TORPEDO,X1_TORPEDO+9},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Kuwanger: Parasitic Bomb */
  {0x0a,1,X2_SLICER,{X1_CUTTER,X1_CUTTER+9},{X1_CUTTER,X1_CUTTER+9}}, /* Chameleon: Sonic Slicer */
  {0x0a,2,X3_BLADE,{X1_CUTTER,X1_CUTTER+9},{X1_CUTTER,X1_CUTTER+9}}, /* Chameleon: Spinning Blade */
  {0x52,1,X2_WHEEL,{X1_STING,X1_STING+9},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Eagle: Spin Wheel */
  {0x52,1,X2_SLICER,{X1_STING,X1_STING+9},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Eagle: Sonic Slicer */
  {0x52,2,X3_BLADE,{X1_STING,X1_STING+9},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Eagle: Spinning Blade */
  {0x0c,1,X2_BUBBLE,{X1_TORNADO,X1_TORNADO+9},{KEEP_ATTACK_CLASS,KEEP_ATTACK_CLASS}}, /* Mammoth: Bubble Splash */
};
/* Which weapon fired player shot `d`: page 0 is X1 (weapon in X1 menu order,
 * Homing Torpedo = 1), 1 is X2 and 2 is X3, by native weapon id. */
bool MmxWeaponsShotIdentity(const uint8_t *r,unsigned d,unsigned *page_out,unsigned *weapon_out,unsigned *charged) {
  if(!r || !slot_valid(d) || !r[d]) return false;
  unsigned page=0,weapon=0;*charged=0;
  if(owned(r,d)) {
    const MmxWeaponShot *s=combat.shots+slot_index(d);
    if(!s->active) return false;
    page=s->page;weapon=s->weapon;*charged=s->charged!=0;
    /* Underwater Speed Burner becomes bubbles/an unlit dash, not fire. */
    if(page==1 && weapon==8 && !native_damage_class(s)) return false;
  } else {
    static const uint8_t x1_classes[]={0,X1_TORPEDO,X1_STING,X1_SHIELD,X1_FIRE,
        X1_TORNADO,X1_SPARK,X1_CUTTER,X1_ICE};
    for(unsigned i=1;i<sizeof(x1_classes);++i) {
      if(r[d+10]==x1_classes[i]) {weapon=i;break;}
      if(r[d+10]==x1_classes[i]+9) {weapon=i;*charged=1;break;}
    }
    if(!weapon) return false;
  }
  *page_out=page;*weapon_out=weapon;return true;
}
static const BossWeakness *boss_weakness(const uint8_t *r,unsigned enemy,unsigned d,unsigned *charged) {
  if(!MmxWeaponsEnabled() || !r || enemy<0xe68 || enemy>=0x1228 ||
      (enemy-0xe68)%64 || !r[enemy] || !slot_valid(d) || !r[d]) return NULL;
  /* Relocated X2/X3 bodies keep an X1 boss class byte for range safety only;
   * Boss Rush owns their weaknesses (mmx_boss_rush_runtime.c). */
  int seat=MmxBossRushOwner(enemy);
  if(seat>=0 && !MmxBossRushNative(MmxBossRushGetState().bosses[seat].id)) return NULL;
  unsigned page=0,weapon=0;
  if(!MmxWeaponsShotIdentity(r,d,&page,&weapon,charged)) return NULL;
  for(unsigned i=0;i<sizeof(boss_weaknesses)/sizeof(boss_weaknesses[0]);++i) {
    const BossWeakness *w=boss_weaknesses+i;
    if(w->boss==r[enemy+10] && w->page==page && w->weapon==weapon) return w;
  }
  return NULL;
}
unsigned MmxWeaponsContactClass(const uint8_t *r,unsigned enemy,unsigned d,unsigned original,bool reaction) {
  unsigned charged;
  const BossWeakness *w=boss_weakness(r,enemy,d,&charged);
  if(!w) return original;
  unsigned value=reaction ? w->reaction[charged] : w->damage[charged];
  if(value==KEEP_ATTACK_CLASS) value=r[d+10];
  return (original&0xff00)|value;
}

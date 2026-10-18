#include "mmx_cart_extension.h"
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include <stdlib.h>
#include <string.h>

enum { IMAGE=0x180000, ORIGINAL=0x200000, EXTENDED=0x400000 };

static size_t offset(uint32_t address) {
  /* LoROM: bank & $7F selects a 32 KB page of the image. */
  return ((size_t)((address>>16)&0x7f)<<15)|(address&0x7fff);
}
static Cart *cart(void) {
  Cart *c=g_snes?g_snes->cart:NULL;
  if(!c || !c->rom || c->type!=CART_LOROM || c->romImageSize!=IMAGE ||
      (c->romSize!=ORIGINAL && c->romSize!=EXTENDED)) return NULL;
  return c;
}
bool MmxCartExtensionReady(void) {
  Cart *c=cart();return c && c->romSize==EXTENDED;
}
bool MmxCartPatchAvailable(void) {return cart()!=NULL;}
bool MmxCartExtensionEnsure(void) {
  if(MmxCartExtensionReady()) return true;
  Cart *c=cart();
  if(!c) return false;
  uint8_t *image=malloc(EXTENDED);
  if(!image) return false;
  memcpy(image,c->rom,ORIGINAL);memcpy(image+ORIGINAL,c->rom,ORIGINAL);
  /* Every ROM read resolves through the cart and g_rom; replace both before
   * freeing the old image. romImageSize keeps the original identity. */
  uint8_t *old=c->rom;c->rom=image;c->romSize=EXTENDED;g_rom=image;
  free(old);
  return true;
}
/* Image range for a write, or SIZE_MAX outside every window. */
static size_t window(uint32_t address,size_t size) {
  Cart *c=cart();
  unsigned bank=(address>>16)&0xff,canonical=bank&0x7f;
  if(!c || (address&0xffff)<0x8000 || size>0x10000-(address&0xffff)) return SIZE_MAX;
  if(canonical>=0x30 && canonical<=0x3f) return offset(address);
  uint32_t low=address&0x7fffff;
  if(canonical==0x06 && low>=(MMX_CART_PAD86_START&0x7fffff) &&
      low+size<=(MMX_CART_PAD86_END&0x7fffff)) return offset(address);
  if(c->romSize==EXTENDED && bank>=MMX_CART_EXTENSION_FIRST_BANK &&
      bank<=MMX_CART_EXTENSION_LAST_BANK) return offset(address);
  return SIZE_MAX;
}
bool MmxCartExtensionWrite(uint32_t address,const void *data,size_t size) {
  size_t at=window(address,size);
  if(at==SIZE_MAX || !data) return false;
  memcpy(g_snes->cart->rom+at,data,size);
  interp_bridge_reset_dynamic_cache(); /* never execute stale decoded code */
  return true;
}
bool MmxCartExtensionRead(uint32_t address,void *data,size_t size) {
  size_t at=window(address,size);
  if(at==SIZE_MAX || !data) return false;
  memcpy(data,g_snes->cart->rom+at,size);
  return true;
}
void MmxCartExtensionClear(void) {
  Cart *c=cart();
  if(!c) return;
  /* $30-$3F mirror the image's last 512 KB; $86's tail is $FF fill. */
  memcpy(c->rom+IMAGE,c->rom+IMAGE-0x80000,ORIGINAL-IMAGE);
  memset(c->rom+offset(MMX_CART_PAD86_START),0xff,MMX_CART_PAD86_END-MMX_CART_PAD86_START);
  if(c->romSize==EXTENDED)
    for(unsigned bank=MMX_CART_EXTENSION_FIRST_BANK;bank<=MMX_CART_EXTENSION_LAST_BANK;++bank) {
      size_t at=offset((uint32_t)bank<<16|0x8000);
      memcpy(c->rom+at,c->rom+at-ORIGINAL,0x8000);
    }
  interp_bridge_reset_dynamic_cache();
}

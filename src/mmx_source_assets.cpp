#include "mmx_source_assets.h"
#include "sha256.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
using Bytes = std::vector<uint8_t>;
namespace fs = std::filesystem;
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
void put(Bytes& b, unsigned v, unsigned n=2) { while (n--) { b.push_back(uint8_t(v)); v >>= 8; } }
void append(Bytes& b, const Bytes& a) { b.insert(b.end(),a.begin(),a.end()); }
struct Rom {
  Bytes data;
  Rom(const char *path, unsigned game) {
    require(path && *path,"Select the original source ROM.");
    std::ifstream f(fs::u8path(path),std::ios::binary|std::ios::ate);
    require(bool(f),"Cannot open the source ROM.");
    auto n=f.tellg(); require(n>0 && n<=8*1024*1024,"Unsupported source ROM size.");
    data.resize(size_t(n)); f.seekg(0); f.read(reinterpret_cast<char*>(data.data()),n);
    require(bool(f),"Cannot read the source ROM.");
    if (data.size()%32768==512) data.erase(data.begin(),data.begin()+512);
    uint8_t digest[32]; sha256_compute(data.data(),data.size(),digest);
    const char *expected=game==2 ? "f3246755f608a1e1dc9c848b61da3b824c7853b29b3be40df6fc7f2793a887ed" :
      "65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7";
    char actual[65]; for (unsigned i=0;i<32;++i) std::snprintf(actual+i*2,3,"%02x",digest[i]);
    require(std::string(actual)==expected,game==2 ? "Select the original Mega Man X2 USA ROM." :
      "Select the original Mega Man X3 USA ROM.");
  }
  size_t offset(unsigned a) const {
    require((a&65535)>=32768,"Invalid source ROM address.");
    return ((a>>16)&127)*32768+(a&32767);
  }
  Bytes raw(size_t p,size_t n) const {
    require(p<=data.size() && n<=data.size()-p,"Source ROM read exceeds bounds.");
    return Bytes(data.begin()+p,data.begin()+p+n);
  }
  Bytes at(unsigned a,size_t n) const { return raw(offset(a),n); }
  unsigned integer(unsigned a,unsigned n=2) const {
    Bytes b=at(a,n); unsigned v=0; for (unsigned i=0;i<n;++i) v|=unsigned(b[i])<<(i*8); return v;
  }
};
struct Tiles { std::array<uint8_t,8192> bytes{}, known{}; };
void transfer(const Rom& r,unsigned table,unsigned pose,Tiles& t) {
  unsigned a=(table&0xff0000)|((table+r.integer(table+pose*2))&65535);
  for (unsigned i=0;i<64;++i,a+=6) {
    unsigned count=r.integer(a,1); if (!count) return;
    unsigned target=r.integer(a+4),start=((target&32767)-0x6000)*2,length=count*16;
    require(start<=8192 && length<=8192-start,"Invalid source sprite transfer.");
    Bytes b=r.at(r.integer(a+1,3),length);
    std::copy(b.begin(),b.end(),t.bytes.begin()+start);
    std::fill_n(t.known.begin()+start,length,1);
    if (target&32768) return;
  }
  throw std::runtime_error("Unterminated source sprite transfer.");
}
void bulk(const Rom& r,unsigned a,Tiles& t) {
  for (unsigned i=0;i<64;++i,a+=7) {
    unsigned length=r.integer(a); if (length&1) return;
    unsigned start=(r.integer(a+2)-0x6000)*2;
    require(start<=8192 && length<=8192-start,"Invalid source selection transfer.");
    Bytes b=r.at(r.integer(a+4,3),length);
    std::copy(b.begin(),b.end(),t.bytes.begin()+start); std::fill_n(t.known.begin()+start,length,1);
  }
  throw std::runtime_error("Unterminated source selection transfer.");
}
struct Pose { int left=0,top=0,width=0,height=0; Bytes pixels; };
Pose pose(const Rom& r,unsigned group,unsigned number,const Tiles& t,bool zero=false,
          unsigned tile_base=0,int actor_palette=-1) {
  unsigned table=r.integer(0x8d8000+group*3,3),a=r.integer(table+number*3,3),n=r.integer(a,1);
  require(n<=64,"Invalid source sprite piece count.");
  Bytes pieces=r.at(a+1,n*4); Pose p;
  if (zero) { p.left=p.top=-64;p.width=p.height=128; }
  else if (n) {
    int right=-256,bottom=-256;p.left=p.top=256;
    for (unsigned i=0;i<n;++i) {
      int x=int8_t(pieces[i*4+1]),y=int8_t(pieces[i*4+2]),size=pieces[i*4]&32?16:8;
      p.left=std::min(p.left,x);p.top=std::min(p.top,y);right=std::max(right,x+size);bottom=std::max(bottom,y+size);
    }
    p.width=right-p.left;p.height=bottom-p.top;
  }
  require(p.width<=256 && p.height<=256,"Source sprite exceeds extraction bounds.");
  p.pixels.resize(p.width*p.height);
  for (int i=int(n)-1;i>=0;--i) {
    unsigned flags=pieces[i*4],tile=(pieces[i*4+3]+tile_base)&255,size=flags&32?16:8;
    int x=int8_t(pieces[i*4+1]),y=int8_t(pieces[i*4+2]);
    require(zero || !(flags&14),"Unexpected source weapon palette.");
    for (unsigned dy=0;dy<size;++dy) for (unsigned dx=0;dx<size;++dx) {
      unsigned tx=flags&64?size-1-dx:dx,ty=flags&128?size-1-dy:dy;
      unsigned number=(((tile>>4)+ty/8)&15)*16+((tile+tx/8)&15),bits=number*32+(ty&7)*2,color=0;
      for (unsigned plane=0;plane<4;++plane) {
        unsigned index=bits+(plane/2)*16+plane%2;
        require(zero || t.known[index],"Unresolved inherited source graphics.");
        color|=((t.bytes[index]>>(7-(tx&7)))&1)<<plane;
      }
      if (color) {
        int px=x+int(dx)-p.left,py=y+int(dy)-p.top;
        require(px>=0 && py>=0 && px<p.width && py<p.height,"Source sprite exceeds canvas.");
        if (zero) color+=(((flags>>1)&7)|(actor_palette>=0?unsigned(actor_palette):group>=0x6f?0:group==0x50?3:1))*16;
        p.pixels[py*p.width+px]=uint8_t(color);
      }
    }
  }
  return p;
}
Bytes menu(const Rom& r,unsigned game,unsigned resource);
Bytes zero_assets(const Rom& r) {
  Bytes out{'M','M','X','Z','E','R','O','7'};
  for (unsigned v : {128,128,64,64,117,35}) put(out,v);
  std::array<unsigned,256> colors{};
  for (unsigned key : {0xd0,0xd2}) {
    unsigned a=0x860000|r.integer(0x868180+key);
    for (unsigned i=0;;++i,a+=4) {
      require(i<32,"Unterminated source palette.");
      unsigned count=r.integer(a,1);if (!count) break;
      unsigned source=0x8c0000|r.integer(a+1),dest=r.integer(a+3,1);
      require(dest+count<=256,"Source palette exceeds CGRAM.");
      for (unsigned j=0;j<count;++j) colors[dest+j]=r.integer(source+j*2);
    }
  }
  for (unsigned i=0;i<16;++i) colors[176+i]=r.integer(0x8cb5a0+i*2);
  for (unsigned i=0;i<16;++i) colors[128+i]=r.integer(0x8cb100+i*2);
  for (unsigned i=0;i<16;++i) colors[160+i]=r.integer(0x8cb0e0+i*2);
  for (unsigned i=128;i<256;++i) put(out,colors[i]);
  Bytes bounds=r.at(0x86b837,40);for (unsigned i=1;i<40;i+=4) bounds[i]-=8;
  append(out,bounds);append(out,r.at(0x2c8d20,64));append(out,r.at(0x2c8de0,64));append(out,r.at(0x8cb0e0,32));
  append(out,r.at(0x3fcc74,0x474));append(out,r.at(0x399161,120));append(out,r.at(0x3991d9,76));
  const unsigned groups[][3]={{0x4a,117,0x85d6a8},{0x4b,21,0x85db47},{0x50,14,0x85e6e0}};
  for (auto& g : groups) { Tiles t;for (unsigned i=0;i<g[1];++i) { transfer(r,g[2],i,t);append(out,pose(r,g[0],i,t,true).pixels); } }
  for (unsigned a : {0x8caf60,0x8caf80,0x8ca5e0}) append(out,r.at(a,32));
  Tiles t; Bytes common=menu(r,3,0x0a);
  require(common.size()==4096,"Unexpected Zero common graphics size.");
  std::copy(common.begin(),common.end(),t.bytes.begin()+0x1000);
  for (unsigned group : {0x6f,0x70,0x71}) for (unsigned i=0;i<22;++i)
    append(out,pose(r,group,i,t,true).pixels);
  return out;
}
struct GroupSource { unsigned id,frames,dma,restricted; uint64_t poses,inherited; int setup; unsigned palette,source;
  unsigned animated_start,layout_start,layout_count,animated_dma; int animation_sequence; };
struct WeaponSource { unsigned game,id,body,palette,extra,groups; int resource; unsigned offset; GroupSource group[3]; };
#include "mmx_weapon_sources.h"
Bytes animations(const Rom& r,unsigned game,unsigned group,int sequence) {
  unsigned base=r.integer((game==2?0x2fa000:0x3f8000)+group*3,3),header=r.integer(base),extent=header;
  require(header && !(header&1) && header<=1024,"Invalid source animation directory.");
  std::vector<unsigned> pending;std::set<unsigned> visited;
  require(sequence<0 || unsigned(sequence)*2<header,"Invalid selected source sequence.");
  if (sequence>=0) pending.push_back(r.integer(base+unsigned(sequence)*2));
  else for (unsigned i=0;i<header;i+=2) pending.push_back(r.integer(base+i));
  while (!pending.empty()) {
    unsigned a=pending.back();pending.pop_back();if (!visited.insert(a).second) continue;
    require(a>=header && a<=8192,"Source animation exceeds group.");
    Bytes rec=r.at(base+a,3); require(rec[0] && rec[2]<128,"Invalid source animation.");
    unsigned end=a+3;
    if (rec[1]&128) { pending.push_back(end+int16_t(r.integer(base+end)));extent=std::max(extent,end+2); }
    else { pending.push_back(end);extent=std::max(extent,end); }
  }
  return r.at(base,extent);
}
Bytes menu(const Rom& r,unsigned game,unsigned resource=0x4c) {
  unsigned rec=(game==2?0x86fa01:0x86f732)+resource*5,length=r.integer(rec+3);
  size_t p=r.offset(r.integer(rec,3));Bytes b;
  while (b.size()<length) {
    unsigned control=r.raw(p++,1)[0];
    for (unsigned bit=128;bit && b.size()<length;bit>>=1) {
      if (control&bit) {
        Bytes pair=r.raw(p,2);p+=2;unsigned count=pair[0]>>2,distance=((pair[0]&3)<<8)|pair[1];
        require(count && distance && distance<=b.size() && b.size()+count<=length,"Invalid source menu backreference.");
        while (count--) b.push_back(b[b.size()-distance]);
      } else b.push_back(r.raw(p++,1)[0]);
    }
  }
  return b;
}
Bytes weapon_assets(const Rom& r,unsigned game) {
  Bytes out{'M','M','X','W','E','A','P','5'};put(out,8,4);Bytes graphics=menu(r,game);
  for (const auto& w : weapon_sources) if (w.game==game) {
    put(out,game,1);put(out,w.id,1);put(out,w.groups,1);put(out,0,1);
    append(out,r.raw(w.body,32));append(out,r.raw(w.palette,32));append(out,r.raw(game==2?0x2cee0:0x62da0,32));
    unsigned tile=w.id<8?0x30+w.id*2:0x50;
    for (unsigned y=0;y<16;++y) for (unsigned x=0;x<16;++x) {
      unsigned start=(game==2?-0x200:0)+(tile+x/8+y/8*16)*32+(y&7)*2,color=0;
      require(start+17<graphics.size(),"Source menu tile exceeds bounds.");
      for (unsigned plane=0;plane<4;++plane) color|=((graphics[start+plane/2*16+plane%2]>>(7-(x&7)))&1)<<plane;
      out.push_back(uint8_t(color));
    }
    Tiles base;
    if (w.resource>=0) {
      Bytes common=menu(r,game,unsigned(w.resource));
      require(w.offset+common.size()<=base.bytes.size(),"Static source graphics exceed tile bank.");
      for (unsigned i=0;i<common.size();++i) {base.bytes[w.offset+i]=common[i];base.known[w.offset+i]=1;}
    }
    bulk(r,0x860000|r.integer((game==2?0x869664:0x8697ad) + 0x3e + w.id*2),base);
    if (w.extra) bulk(r,w.extra,base);
    // Original gameplay footer, including its frame. Palette is the weapon
    // palette already stored above, matching source OAM palette 3.
    unsigned hud_tile=game==2 ? 0x28 : 0xac;
    for (unsigned y=0;y<16;++y) for (unsigned x=0;x<16;++x) {
      unsigned start=(hud_tile+x/8+y/8*16)*32+(y&7)*2,color=0;
      for (unsigned plane=0;plane<4;++plane) {
        unsigned offset=start+plane/2*16+plane%2;
        require(base.known[offset],"Unresolved source gameplay HUD graphics.");
        color|=((base.bytes[offset]>>(7-(x&7)))&1)<<plane;
      }
      out.push_back(uint8_t(color));
    }
    for (unsigned j=0;j<w.groups;++j) {
      const auto& g=w.group[j];Tiles t=base;if (g.setup>=0) transfer(r,g.dma,unsigned(g.setup),t);
      Bytes a=animations(r,game,g.source,g.animation_sequence);put(out,g.id);put(out,g.frames);put(out,unsigned(a.size()));
      append(out,r.raw(g.palette,32));append(out,a);
      for (unsigned i=0;i<g.frames;++i) {
        bool animated=g.animated_start && i>=g.animated_start;unsigned layout=i;
        if (!animated && g.restricted && (i>=64 || !(g.poses&(uint64_t(1)<<i)))) { out.insert(out.end(),8,0);continue; }
        if (animated) {
          require(g.layout_count && (i-g.animated_start)/g.layout_count<4,"Invalid source animated CHR phase.");
          t=base;bulk(r,0x860000|r.integer(g.animated_dma+(i-g.animated_start)/g.layout_count*2),t);
          layout=g.layout_start+(i-g.animated_start)%g.layout_count;
        } else if (i>=64 || !(g.inherited&(uint64_t(1)<<i))) transfer(r,g.dma,i,t);
        Pose p=pose(r,g.source,layout,t);put(out,unsigned(p.left));put(out,unsigned(p.top));put(out,p.width);put(out,p.height);append(out,p.pixels);
      }
    }
  }
  return out;
}
void cropped_pose(Bytes& out,const Pose& p) {
  int l=128,top=128,right=0,bottom=0;
  for(int y=0;y<128;++y) for(int x=0;x<128;++x)
    if(p.pixels[y*128+x]) {l=std::min(l,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
  if(right<=l || bottom<=top) {out.insert(out.end(),8,0);return;}
  put(out,unsigned(l));put(out,unsigned(top));put(out,unsigned(right-l));put(out,unsigned(bottom-top));
  for(int y=top;y<bottom;++y) out.insert(out.end(),p.pixels.begin()+y*128+l,p.pixels.begin()+y*128+right);
}
Bytes player_extended_animation(const Rom& r) {
  /* X's extended directory precedes Zero's saber directory. Their offsets
   * have different bases; the first offset is not a directory byte count. */
  const unsigned base=0x3fd09e;
  unsigned extent=138;
  std::vector<unsigned> pending;std::set<unsigned> visited;
  for(unsigned i=0;i<37;++i)pending.push_back(r.integer(base+i*2));
  while(!pending.empty()) {
    unsigned at=pending.back();pending.pop_back();if(!visited.insert(at).second)continue;
    require(at>=138 && at<8192,"Invalid extended X animation offset.");
    Bytes rec=r.at(base+at,3);require(rec[0] && rec[2]<48,"Invalid extended X animation record.");
    unsigned end=at+3;
    if(rec[1]&128) {pending.push_back(end+int16_t(r.integer(base+end)));extent=std::max(extent,end+2);}
    else {pending.push_back(end);extent=std::max(extent,end);}
  }
  return r.at(base,extent);
}
Bytes armor_assets(const Rom& r,unsigned game) {
  /* The original player DMA includes all three separate armor planes. The
   * helmet switches the base-body arrangement; it is not a fourth overlay.
   * X2 $88: B97C / X3 $84:A63C and $81:836B select these same groups. */
  const unsigned frames=game==2?112:100;
  const unsigned dma=game==2?0x859609:0x85ce45;
  const unsigned anim=game==2?0x2fa222:0x3fcb64;
  const unsigned anim_size=game==2?0x41e:0x584;
  Bytes out{'M','M','X','A','R','M','R','1'};
  put(out,game);put(out,frames);put(out,5);put(out,game==2?133:136);
  put(out,anim_size);put(out,game==2?266:544);
  append(out,r.raw(game==2?0x2b100:0x62400,32));
  /* X3 $84:B713 loads palette key $1B8 ($86:8750 -> $8C:A580). */
  append(out,r.raw(game==2?0x2b100:0x62580,32));
  append(out,r.at(anim,anim_size));
  Tiles t;
  for (unsigned i=0;i<frames;++i) {
    transfer(r,dma,i,t);
    for (unsigned group : {0u,0x18u,0x5du,0x5eu,0x5fu}) {
      cropped_pose(out,pose(r,group,i,t,true));
    }
  }
  struct Extra {unsigned group,frames,dma,tile,palette,source=0;};
  std::vector<Extra> extra;
  if(game==3)for(unsigned group=0x33;group<=0x37;++group)extra.push_back({group,48,0x85d334,0,1});
  extra.push_back({8,14,0,0,2});
  extra.push_back({14,21,game==2?0x859bfcu:0x85dc19u,0,3});
  extra.push_back({0x9e,game==2?12u:16u,game==2?0x859cc1u:0x85dc75u,0,3});
  extra.push_back({0x54,game==2?13u:10u,game==2?0x859c58u:0x85dd2du,game==2?8u:0u,3});
  if(game==2)extra.push_back({0x55,13,0x859c58,12,3,0x54});
  /* X2's standard pilot DMA covers 36 arrangements; the remaining group
   * entries belong to a separate Ride Chaser transfer table. */
  extra.push_back({0x6a,game==2?36u:47u,game==2?0x859b46u:0x85d568u,0,1});
  extra.push_back({0x6b,game==2?36u:47u,game==2?0x859b46u:0x85d568u,0,1});
  if(game==3) {extra.push_back({0x3a,12,0x85dcf0,8,3});extra.push_back({0x65,13,0,0,2});}
  append(out,Bytes{'A','R','F','X','0','0','0','1'});put(out,unsigned(extra.size()));put(out,0);
  for(unsigned address : game==2?std::array<unsigned,3>{0x2b240,0x2b220,0x2b160}:
                               std::array<unsigned,3>{0x63100,0x630e0,0x62460})append(out,r.raw(address,32));
  for(const auto& g:extra) {
    unsigned source=g.source?g.source:g.group;
    Bytes a;
    if(g.group==0x33)a=player_extended_animation(r);
    else if(!((g.group>=0x34 && g.group<=0x37) || g.group==0x6a || g.group==0x6b))
      a=animations(r,game,source,-1);
    put(out,g.group);put(out,g.frames);put(out,unsigned(a.size()));
    put(out,g.group==0x33?37:a.empty()?0:unsigned(a[0]|a[1]<<8)/2);
    put(out,g.group==0x33?138:a.empty()?0:unsigned(a[0]|a[1]<<8));put(out,0);
    append(out,a);
    Tiles tiles;Bytes common=menu(r,game,0x0a);
    require(common.size()==4096,"Unexpected common armor graphics size.");
    std::copy(common.begin(),common.end(),tiles.bytes.begin()+0x1000);
    std::fill(tiles.known.begin()+0x1000,tiles.known.end(),1);
    for(unsigned i=0;i<g.frames;++i) {
      if(g.dma)transfer(r,g.dma,i,tiles);
      cropped_pose(out,pose(r,source,i,tiles,true,g.tile,int(g.palette)));
    }
  }
  /* Small original data tables, retained only in the owner's verified cache.
   * The host port uses these exact muzzle positions, bounds and segment
   * velocities rather than approximating the charged shots with sine waves. */
  append(out,Bytes{'A','R','C','F','0','0','0','1'});put(out,10);put(out,0);
  auto config=[&](unsigned id,unsigned address,unsigned length) {
    put(out,id);put(out,length);append(out,r.at(address,length));
  };
  if(game==2)config(1,0x86b5f6,256);
  else {put(out,1);put(out,356);append(out,r.at(0x399000,100));append(out,r.at(0x399064,256));}
  config(2,game==2?0x86ba4d:0x86bb1a,8);
  config(3,game==2?0x86ba55:0x86bb22,game==2?92:117);
  config(4,game==2?0x86f4c8:0x86e55d,36);
  config(5,game==2?0x86b5da:0x86b7fb,4);
  config(6,game==2?0x86ba4d:0x86bb98,game==2?4:64);
  put(out,7);put(out,64);
  append(out,r.raw(game==2?0x2c2a0:0x62f80,32));append(out,r.raw(game==2?0x2c2a0:0x625a0,32));
  config(8,game==2?0x86b5de:0x86b7ff,24);
  put(out,9);put(out,4);if(game==2)append(out,r.at(0x86b7ce,4));else put(out,0,4);
  put(out,10);put(out,2);if(game==3)append(out,r.at(0x86b364,2));else put(out,0);
  return out;
}
void publish(const char *output,const Bytes& bytes) {
  fs::path path=fs::u8path(output),temp=path;
  /* Two local peers can prepare the same verified ROM concurrently. Each
   * writer publishes a complete pack atomically from its own temporary file. */
#ifdef _WIN32
  temp+="."+std::to_string(GetCurrentProcessId())+".tmp";
#else
  temp+="."+std::to_string(getpid())+".tmp";
#endif
  if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
  try {
    std::ofstream f(temp,std::ios::binary|std::ios::trunc);require(bool(f),"Cannot create local mod cache.");
    f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));f.close();
    require(bool(f),"Cannot write local mod cache.");
#ifdef _WIN32
    require(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot publish local mod cache.");
#else
    fs::rename(temp,path);
#endif
  } catch (...) { std::error_code ec;fs::remove(temp,ec);throw; }
}
}
int MmxArmorAssetsBuild(const char *rom,unsigned game,const char *output,char *error,size_t size) {
  try {
    require((game==2 || game==3) && output,"Invalid armor asset request.");
    Rom r(rom,game);publish(output,armor_assets(r,game));
    if(error && size) *error=0;
    return 1;
  } catch(const std::exception& e) {
    if(error && size) std::snprintf(error,size,"%s",e.what());
    return 0;
  }
}
int MmxSourceAssetsBuild(const char *rom,unsigned game,int zero,const char *output,char *error,size_t size) {
  try {
    require((game==2 || game==3) && (!zero || game==3) && output,"Invalid source asset request.");
    Rom r(rom,game);publish(output,zero?zero_assets(r):weapon_assets(r,game));
    if (error && size) error[0]=0;return 1;
  } catch (const std::exception& e) { if (error && size) std::snprintf(error,size,"%s",e.what());return 0; }
}

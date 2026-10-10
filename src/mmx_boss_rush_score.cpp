#include "mmx_boss_rush_score.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
namespace fs=std::filesystem;
constexpr char header[]="MMX-BOSS-RUSH 1\n";
fs::path save_path;
uint32_t best;
bool writable;
}
extern "C" uint32_t MmxBossRushHighScore(void) {return best;}
extern "C" bool MmxBossRushScoreOpen(const char *path) {
  best=0;writable=false;save_path.clear();
  if(!path || !*path) return false;
  save_path=fs::u8path(path);std::error_code ec;
  bool exists=fs::exists(save_path,ec);if(ec) return false;
  if(exists) {
    auto size=fs::file_size(save_path,ec);if(ec || size>64) return false;
    std::ifstream in(save_path,std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)),{});
    const std::string prefix(header);
    if(!in || data.size()<=prefix.size()+1 || data.compare(0,prefix.size(),prefix) || data.back()!='\n') return false;
    uint32_t value=0;const char *begin=data.data()+prefix.size(),*end=data.data()+data.size()-1;
    auto parsed=std::from_chars(begin,end,value);
    if(parsed.ec!=std::errc() || parsed.ptr!=end) return false;
    best=value;
  }
  writable=true;return true;
}
extern "C" bool MmxBossRushScoreRecord(uint32_t defeated) {
  if(defeated<=best) return true;
  best=defeated;
  if(!writable) return false;
  fs::path temporary=save_path;temporary+=".tmp";
  std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
  out<<header<<best<<'\n';out.close();
  bool ok=!!out;
  if(ok) {
#ifdef _WIN32
    ok=MoveFileExW(temporary.c_str(),save_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code ec;fs::rename(temporary,save_path,ec);ok=!ec;
#endif
  }
  if(!ok) writable=false;
  return ok;
}

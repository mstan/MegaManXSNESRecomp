#include "mmx_boss_rush_score.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

static std::string read(const std::filesystem::path &path) {
  std::ifstream in(path,std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)),{});
}
int main() {
  const auto path=std::filesystem::temp_directory_path()/"mmx-rush-score-test.dat";
  std::filesystem::remove(path);
  assert(MmxBossRushScoreOpen(path.u8string().c_str()));
  assert(MmxBossRushScoreRecord(23));
  const std::string saved=read(path);
  assert(MmxBossRushScoreRecord(7));assert(read(path)==saved);
  assert(MmxBossRushScoreOpen(path.u8string().c_str()));assert(MmxBossRushHighScore()==23);
  assert(MmxBossRushScoreRecord(42));
  assert(MmxBossRushScoreOpen(path.u8string().c_str()));assert(MmxBossRushHighScore()==42);
  for(const char *bad:{"unrecognized data","MMX-BOSS-RUSH 1\n-1\n","MMX-BOSS-RUSH 1\n4294967296\n"}) {
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bad;}
    assert(!MmxBossRushScoreOpen(path.u8string().c_str()));
    assert(!MmxBossRushScoreRecord(100));assert(MmxBossRushHighScore()==100);
    assert(read(path)==bad);
  }
  std::filesystem::remove(path);
}

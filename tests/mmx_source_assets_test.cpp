#include "mmx_source_assets.h"
#include <cstdio>
#include <cstdlib>
int main(int argc,char **argv) {
  if (argc!=5) { std::fprintf(stderr,"Usage: extractor ROM GAME ZERO OUTPUT\n");return 2; }
  char error[512];
  int ok=std::atoi(argv[3])==2 ? MmxArmorAssetsBuild(argv[1],unsigned(std::atoi(argv[2])),argv[4],error,sizeof(error)) :
      MmxSourceAssetsBuild(argv[1],unsigned(std::atoi(argv[2])),std::atoi(argv[3]),argv[4],error,sizeof(error));
  if (!ok) {
    std::fprintf(stderr,"%s\n",error);return 1;
  }
  return 0;
}

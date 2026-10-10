#include "mmx_saber_assets.h"

#include <stdio.h>
#include <string.h>

static int hex_digit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

static int parse_sha256(const char *text, unsigned char digest[32]) {
  size_t i;
  if (!text || strlen(text) != 64) return 0;
  for (i = 0; i < 32; ++i) {
    int high = hex_digit(text[i * 2]);
    int low = hex_digit(text[i * 2 + 1]);
    if (high < 0 || low < 0) return 0;
    digest[i] = (unsigned char)((high << 4) | low);
  }
  return 1;
}

int main(int argc, char **argv) {
  const char *path;
  int ride = 0;
  unsigned char expected_digest[32];
  const unsigned char *expected = NULL;
  char reason[128];
  MmxSaberAssets *assets;

  if (argc >= 3 && !strcmp(argv[1], "--ride")) {
    ride = 1;
    path = argv[2];
    if (argc == 4) {
      if (!parse_sha256(argv[3], expected_digest)) {
        fprintf(stderr, "mmx_saber_check: expected manifest hash is not 64 hex digits\n");
        return 1;
      }
      expected = expected_digest;
    } else if (argc != 3) {
      fprintf(stderr, "usage: mmx_saber_check --ride FILE [EXPECTED_MANIFEST_SHA256_HEX]\n");
      return 1;
    }
  } else if (argc == 2) {
    path = argv[1];
  } else if (argc == 3) {
    path = argv[1];
    if (!parse_sha256(argv[2], expected_digest)) {
      fprintf(stderr, "mmx_saber_check: expected manifest hash is not 64 hex digits\n");
      return 1;
    }
    expected = expected_digest;
  } else {
    fprintf(stderr, "usage: mmx_saber_check FILE [EXPECTED_MANIFEST_SHA256_HEX]\n");
    return 1;
  }

  assets = MmxSaberAssetsLoadFile(path, expected, reason, sizeof(reason));
  if (!assets) {
    fprintf(stderr, "mmx_saber_check: %s\n", reason[0] ? reason : "parse failed");
    return 1;
  }
  if (ride) {
    const MmxSaberAnimation *animation = MmxSaberAssetsAnimationById(assets, 0x006b);
    if (!animation || animation->step_count != 23) {
      fprintf(stderr, "mmx_saber_check: ride animation 0x006b must contain 23 steps\n");
      MmxSaberAssetsFree(assets);
      return 1;
    }
    for (unsigned pose = 0; pose < 23; ++pose) {
      if (!MmxSaberAssetsFrameForStep(assets, 0x006b, (uint16_t)pose)) {
        fprintf(stderr, "mmx_saber_check: ride pose %u has no frame\n", pose);
        MmxSaberAssetsFree(assets);
        return 1;
      }
    }
  }
  MmxSaberAssetsFree(assets);
  puts(ride ? "mmx_saber_check: ride ok" : "mmx_saber_check: ok");
  return 0;
}

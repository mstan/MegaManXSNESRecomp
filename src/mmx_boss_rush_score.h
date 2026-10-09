#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Local results metadata, deliberately outside rollback/gameplay state. */
bool MmxBossRushScoreOpen(const char *path);
bool MmxBossRushScoreRecord(uint32_t defeated);
uint32_t MmxBossRushHighScore(void);
#ifdef __cplusplus
}
#endif

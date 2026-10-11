// Compatibility header for OpenOrbis distributions that lack <orbis/NpActivity.h>.
// The functions are implemented locally in np_activity_ps4.cpp (no PSN presence
// is pushed), so this only has to agree with that file.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceNpActivityPresenceStatus {
    char presenceKey[64];
    char statusString[256];
} SceNpActivityPresenceStatus;

int32_t sceNpActivityInitPresenceStatus(SceNpActivityPresenceStatus* status);
int32_t sceNpActivitySetPresenceStatusString(SceNpActivityPresenceStatus* status,
                                             const char* key, const char* value);
int32_t sceNpActivitySetPresence(int32_t userId, const SceNpActivityPresenceStatus* status);

#ifdef __cplusplus
}
#endif

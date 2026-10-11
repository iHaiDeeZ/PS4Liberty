// Compatibility header for OpenOrbis distributions that lack <orbis/libc/stdlib.h>
// (e.g. PacBrew's): the sceLibcMspace allocator API used by user_malloc_ps4.cpp
// (implemented in mspace_ps4.cpp) plus the direct-memory constants it needs.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* SceLibcMspace;

typedef struct SceLibcMallocManagedSize {
    uint16_t size;
    uint16_t version;
    uint32_t reserved1;
    size_t maxSystemSize;
    size_t currentSystemSize;
    size_t maxInuseSize;
    size_t currentInuseSize;
} SceLibcMallocManagedSize;

SceLibcMspace sceLibcMspaceCreate(const char* name, void* base, size_t capacity, uint32_t flags);
int sceLibcMspaceDestroy(SceLibcMspace msp);
void* sceLibcMspaceMalloc(SceLibcMspace msp, size_t size);
void sceLibcMspaceFree(SceLibcMspace msp, void* ptr);
void* sceLibcMspaceCalloc(SceLibcMspace msp, size_t nelem, size_t size);
void* sceLibcMspaceRealloc(SceLibcMspace msp, void* ptr, size_t size);
void* sceLibcMspaceMemalign(SceLibcMspace msp, size_t boundary, size_t size);
int sceLibcMspacePosixMemalign(SceLibcMspace msp, void** ptr, size_t boundary, size_t size);
void* sceLibcMspaceReallocalign(SceLibcMspace msp, void* ptr, size_t boundary, size_t size);
int sceLibcMspaceMallocStats(SceLibcMspace msp, SceLibcMallocManagedSize* mmsize);
int sceLibcMspaceMallocStatsFast(SceLibcMspace msp, SceLibcMallocManagedSize* mmsize);
size_t sceLibcMspaceMallocUsableSize(void* ptr);
int sceLibcMspaceIsHeapEmpty(SceLibcMspace msp);

#ifdef __cplusplus
}
#endif

// Direct-memory constants fixed by the PS4 kernel ABI.
#ifndef SCE_KERNEL_WB_ONION
#define SCE_KERNEL_WB_ONION 0
#endif
#ifndef SCE_KERNEL_PROT_CPU_READ
#define SCE_KERNEL_PROT_CPU_READ 0x01
#endif
#ifndef SCE_KERNEL_PROT_CPU_WRITE
#define SCE_KERNEL_PROT_CPU_WRITE 0x02
#endif
#ifndef SCE_KERNEL_MAIN_DMEM_SIZE
// Search the whole direct-memory pool, as the OpenOrbis samples do.
#define SCE_KERNEL_MAIN_DMEM_SIZE ((off_t)sceKernelGetDirectMemorySize())
#endif

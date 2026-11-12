#include "common.h"
extern int __OSCurrHeap;

void *Allocate(u32 size, u32 flags, const char *name)
{
    void *ptr;

    if (size == 0) {
        return 0;
    }
    ptr = OSAllocFromHeap(__OSCurrHeap, size);
    if (ptr == 0) {
        Dprintf("ALLOCATE ERROR Could not allocate the requested memory, going to crash!!\n");
        /* port: say so and stop, rather than quit or draw without models
         * (the retail data peaks at about 16 MB of the 128; mods can add) */
        Port_Fatal("The game ran out of memory: %u bytes asked of its %u MB heap (the log has the use). "
                   "Turn off mods that add large packs, or raise HWVX_ARENA_MB.", size, Port_GameHeapMB());
    }
    return ptr;
}

void Deallocate(void *ptr)
{
    if (ptr != 0) {
        OSFreeToHeap(__OSCurrHeap, ptr);
    }
}

void MemCpy32(void *dst, const void *src, u32 size)
{
    memcpy(dst, src, size);
}

void MemClr32(void *dst, unsigned long size)
{
    memset(dst, 0, size);
}

void HeapDump(void) {

}



extern int __OSCurrHeap;

void *Allocate(u32 size, u32 flags, const char *name)
{
    void *ptr;

    if (size == 0) {
        return 0;
    }
    ptr = PTR(OSAllocFromHeap(__OSCurrHeap, size));
    if (ptr == 0) {
        Dprintf("ALLOCATE ERROR Could not allocate the requested memory, going to crash!!\n");
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

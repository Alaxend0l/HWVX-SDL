#ifndef _MSL_FILE_H
#define _MSL_FILE_H

#include "common.h"

typedef struct __file_modes {
    u32 open_mode : 2;
    u32 io_mode : 3;
    u32 buffer_mode : 2;
    u32 file_kind : 3;
    u32 file_orientation : 2;
    u32 binary_io : 1;
} __file_modes;

typedef struct __file_state {
    u32 io_state : 3;
    u32 free_buffer : 1;
    u8 eof;
    u8 error;
} __file_state;

typedef struct _FILE {
    u32 handle;
    __file_modes mode;
    __file_state state;
    u8 is_dynamically_allocated;
    u8 unkD[0x18 - 0xD];
    u32 position;
    PTR32(u8) buffer;
    u32 buffer_size;
    PTR32(u8) buffer_ptr;
    u32 buffer_len;
    u32 buffer_alignment;
    u32 saved_buffer_len;
    u32 buffer_pos;
    PTR32(void) position_fn;
    PTR32(void) read_fn;
    FPTR32(s32, (u32 handle, u8 *buffer, u32 *count, void *ref_con)) write_fn;
    FPTR32(s32, (u32 handle)) close_fn;
    PTR32(void) ref_con;
    PTR32(struct _FILE) next_file_struct;
} FILE;

extern int errno;

#endif

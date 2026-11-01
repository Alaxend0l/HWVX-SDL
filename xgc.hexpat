// Enter your pattern code here and click on the Play button
// below to execute your code. The results will then be visible
// in the Pattern Data view.
// 
// More information can be found in the documentation.
// 
// 
// Simple example:
// 
// import std.io;
// 
// struct Pattern {
//     u32 int;
//     float floating_point;
// };
// 
// Pattern my_pattern @ 0x00;
// std::print("0x{:08X}", my_pattern.int);

#pragma endian big


fn divisible(u32 ptr, u32 by) {
    u32 remainder = ptr % by;
    return remainder == 0 ? remainder : remainder + (by - remainder);
};

union Ptr32<T> {
    u32 h;
    u32 *pointerToT : T;
};

struct DirectoryEntry {
    u32 version;
    u32 type;
    u32;
    u32;
    u32 length;
    u32 offset;
};

struct DataPackRef {
    char name[0xc];
    u32 id;
    Ptr32<u8> pack;
};

struct IndexPatch {
    u32 offset;
    u16 index;
    u16 linked_file;
};

struct IndexPatchList<auto TextureCount, auto TextureAnimCount, auto SoundsCount>  {
    IndexPatch textures[TextureCount];
    IndexPatch texture_anims[TextureAnimCount];
    IndexPatch sounds[SoundsCount];
};


struct Audio {
    u32 offset_data;
    u32 data_length;
    u32;
    u32 sample_rate;
    u16;
    u16;
    u32;
    u32;
    u32;
};

struct SoundOffset {
    u32 offset;
};

struct Texture {
    u16 type;
    u16 x;
    u16 y;
    u16 mips;
    u32 offset;
    u32 pallette_index;
};

struct OLBasic {
    u32;
    u32 offset;
    u32;
    u32;
};


struct DataPack<auto offset> {
    u32 dataSize; // 00
    u32 soundDataSize; // 04
    u32 numSoundPointers; // 08
    u32 numPointers; // 12
    u32 numTextureRelocs; // 16
    u32 numTextures; // 20
    u32 textureDataOffset; // 24
    u32 numSoundRelocs; // 28
    u32 numSounds; // 32
    u32 sfxOffset; // 36
    u32 palettes; // 40
    u32 paletteOffset; // 44
    u32 numShaders; // 48
    u32 numGX; // 52
    u32 numTexRelocs; // 56
    u32 shaderOffset; // 60
    u32 numTriggers; // 64
    u32 triggerOffset; // 68
    u32 songOffset; // 72
    u32 songBankOffset; // 76
    DataPackRef references[2];
    SoundOffset sounds_offsets[numSoundPointers];
    Audio audios[numSounds];
    
    u32 length = 120;
    u32 offset_audio_sec = addressof(this) + length;
    u32 offset_ipl = offset_audio_sec;
    u32 after_1st_audio_sec = 0;
    if (soundDataSize > 0) {
        offset_ipl += divisible(numSoundPointers * 4, 32);
        after_1st_audio_sec = offset_ipl;
        offset_ipl += soundDataSize;
    }
    
    u32 ipl_length = numTextureRelocs + numGX + numSoundRelocs;
    ipl_length *= 8;
    ipl_length = divisible(ipl_length, 32);
    
    
    u32 offset_ordered_list = offset_ipl + ipl_length;
    u32 offset_opf = offset_ordered_list + dataSize;
    u32 offset_textures = offset_ordered_list + textureDataOffset;
    
    u32 ipl = addressof(this) + 120;
    ipl += numSoundPointers * 4;
    ipl += soundDataSize;
    
    u32 ordered_list = ipl;
    ordered_list += numTextureRelocs * sizeof(IndexPatch);
    ordered_list += numGX * sizeof(IndexPatch);
    ordered_list += numSoundRelocs * sizeof(IndexPatch);
    
    
    IndexPatchList<numTextureRelocs, numGX, numSoundRelocs> patch_list @ offset_ipl;
    Texture tex1 @ offset_textures;
    OLBasic @ ordered_list;
};

struct Header {
    u32 magic1;
    u32 magic2;
    u32 version;
    u32 count;
    DirectoryEntry entries[count];
    DataPack<$> dp1;
};

Header header @ 0x0;
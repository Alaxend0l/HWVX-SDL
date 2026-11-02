/* Input glyph sets: replace the GameCube button prompts with the glyphs of
 * another controller family (src/game/Port_Glyphs.c). Port-only.
 *
 * The button glyphs are textures inside streamed data packs, addressed by
 * pack-local index. DataPack_RefreshTextures() loads every pack texture into
 * the global texture table and fills textureIDs[]; the reloc loop right after
 * hands those slot numbers to everything that references them. A glyph set
 * therefore swaps a glyph by rewriting one textureIDs[] entry between the
 * two loops: the replacement is loaded with the game's own loose-texture
 * loader (Gfx_LoadTexture: <data>/<name>.btf / .tga, looked up case
 * insensitively), every reference keeps pointing at a valid slot, and no
 * pack byte is touched. Overrides re-apply on every refresh, because
 * RefreshTextures reloads the whole table each time a pack streams back in.
 *
 * A replacement should match the original texture's width and height - the
 * quads the game draws the glyph with carry fixed scale and UV factors - but
 * the format comes from the image file, so sets may be richer than the
 * originals. */
#ifndef PORT_GLYPHS_H
#define PORT_GLYPHS_H

#include "port.h"

struct DataPack;

/* One glyph replacement: texture number textureIndex of the data pack
 * `packName` entry `packId` (the identity DataPack_LoadFile uses) becomes the
 * texture loaded under name texName. */
struct GlyphOverride {
    const char *packName; /* data pack base name, e.g. "HW_IPack" */
    u32 packId;           /* entry id within that file */
    u32 textureIndex;     /* texture number within the entry */
    const char *texName;  /* Gfx_LoadTexture name, e.g. "glyphs/xbox/a" */
};

/* The built-in sets (gamecube = the original art, no overrides) are
 * registered automatically by Port_Glyphs_Select(). Sets may also extend or
 * replace built-in overrides with glyphs/<set>/glyphs.txt in the data tree
 * ("override <packName> <packId> <textureIndex> <texName>" lines); an
 * unregistered set name is usable that way. */
int Port_Glyphs_Select(const char *name); /* NULL clears; 0 for unknown set */
const char *Port_Glyphs_KnownSets(void);

/* DataPack_LoadFile records the identity of the pack it is loading (the
 * DataPack_RefreshTextures hook cannot recover it: later refreshes do not go
 * through the loader). */
void Port_Glyphs_RecordPack(const void *pack, const char *name, u32 id);
void Port_Glyphs_ApplyToPack(struct DataPack *pack);

#endif

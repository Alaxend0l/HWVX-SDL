/* A small JSONC reader and writer (JSON plus // and block comments and
 * trailing commas) for the port's own text files: the save store
 * (game/Port_Saves.c) first. No game types here. */
#ifndef JSONC_H
#define JSONC_H

#include <stddef.h>

typedef enum JsonType {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonNode {
    JsonType type;
    char *key;                /* the member's name, in an object */
    int boolean;
    double number;
    long long integer;        /* the number when it was written without a fraction or exponent */
    int isInteger;
    char *string;             /* JSON_STRING, UTF-8, NUL-terminated */
    struct JsonNode *child;   /* first element or member */
    struct JsonNode *next;    /* next sibling */
    int count;                /* elements or members */
} JsonNode;

/* Parses `len` bytes; NULL on error, with a message (and line) in err. */
JsonNode *Json_Parse(const char *text, size_t len, char *err, size_t errSize);
void Json_Free(JsonNode *node);

/* The member `key` of an object, or NULL. */
const JsonNode *Json_Get(const JsonNode *obj, const char *key);
/* Element i of an array, or NULL. */
const JsonNode *Json_At(const JsonNode *arr, int i);
/* Typed lookups with a fallback when the member is missing or of another type. */
long long Json_GetInt(const JsonNode *obj, const char *key, long long fallback);
double Json_GetNumber(const JsonNode *obj, const char *key, double fallback);
const char *Json_GetString(const JsonNode *obj, const char *key, const char *fallback);
int Json_GetBool(const JsonNode *obj, const char *key, int fallback);

/* Building trees: a deep copy of one value (its key too, not its
 * siblings); an empty object; `child` appended to an array or object (it
 * takes the copy's ownership; key replaces the child's for an object). */
JsonNode *Json_Clone(const JsonNode *node);
JsonNode *Json_NewObject(void);
void Json_Append(JsonNode *container, JsonNode *child, const char *key);
/* The member `key` of an object, created as an empty object if missing. */
JsonNode *Json_ObjectMember(JsonNode *obj, const char *key);
/* New values. */
JsonNode *Json_NewBool(int v);
JsonNode *Json_NewInt(long long v);
/* Removes and frees the member `key` of an object: 1 if it was there. */
int Json_RemoveMember(JsonNode *obj, const char *key);

/* The deepest nesting Json_Parse reads (containers, the root's included).
 * The writer goes deeper, so whatever was read (a save's kept entries) can
 * be written back. */
#define JSON_MAX_DEPTH 64

/* Writer: pretty-printed, two spaces an indent. A container opened "inline"
 * keeps its elements on one line (short number lists). */
typedef struct JsonWriter {
    char *buf;
    size_t len, cap;
    int depth;
    int failed;               /* out of memory or misuse */
    unsigned char count[JSON_MAX_DEPTH + 16];  /* elements so far at each depth (capped at 2: only 0/1/more matters) */
    unsigned char inlined[JSON_MAX_DEPTH + 16];
} JsonWriter;

void Jw_Init(JsonWriter *w);
void Jw_Free(JsonWriter *w);
void Jw_Comment(JsonWriter *w, const char *text);     /* a // line, at the current indent */
/* key: NULL inside an array. */
void Jw_BeginObject(JsonWriter *w, const char *key, int inlined);
void Jw_EndObject(JsonWriter *w);
void Jw_BeginArray(JsonWriter *w, const char *key, int inlined);
void Jw_EndArray(JsonWriter *w);
void Jw_Int(JsonWriter *w, const char *key, long long v);
void Jw_Float(JsonWriter *w, const char *key, float v);   /* exact: reads back to the same bits */
void Jw_Bool(JsonWriter *w, const char *key, int v);
void Jw_String(JsonWriter *w, const char *key, const char *s);
void Jw_Hex(JsonWriter *w, const char *key, const void *data, size_t size);
void Jw_Node(JsonWriter *w, const char *key, const JsonNode *node);  /* a parsed value, copied */
void Jw_Raw(JsonWriter *w, const char *text);   /* appended as is */

/* "0a1b..." back to bytes: 1 if `s` is exactly 2*size hex digits. */
int Json_HexDecode(const char *s, void *out, size_t size);

#endif

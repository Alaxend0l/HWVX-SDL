/* A small JSONC reader and writer: see jsonc.h. */
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "jsonc.h"

/* ---- reader ------------------------------------------------------------ */

typedef struct Parser {
    const char *p, *end, *start;
    char *err;
    size_t errSize;
    int failed;
    int depth;
} Parser;

static void Fail(Parser *ps, const char *what) {
    int line = 1;
    const char *q;

    if (ps->failed) {
        return;
    }
    ps->failed = 1;
    for (q = ps->start; q < ps->p && q < ps->end; q++) {
        line += *q == '\n';
    }
    if (ps->err != NULL && ps->errSize > 0) {
        SDL_snprintf(ps->err, ps->errSize, "line %d: %s", line, what);
    }
}

static void SkipSpace(Parser *ps) {
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ps->p++;
        } else if (c == '/' && ps->p + 1 < ps->end && ps->p[1] == '/') {
            while (ps->p < ps->end && *ps->p != '\n') {
                ps->p++;
            }
        } else if (c == '/' && ps->p + 1 < ps->end && ps->p[1] == '*') {
            ps->p += 2;
            while (ps->p + 1 < ps->end && !(ps->p[0] == '*' && ps->p[1] == '/')) {
                ps->p++;
            }
            if (ps->p + 1 >= ps->end) {
                Fail(ps, "unterminated comment");
                ps->p = ps->end;
                return;
            }
            ps->p += 2;
        } else {
            return;
        }
    }
}

static JsonNode *NewNode(JsonType type) {
    JsonNode *n = SDL_calloc(1, sizeof(JsonNode));
    if (n != NULL) {
        n->type = type;
    }
    return n;
}

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void PutUtf8(char *out, size_t *n, unsigned cp) {
    if (cp < 0x80) {
        out[(*n)++] = (char)cp;
    } else if (cp < 0x800) {
        out[(*n)++] = (char)(0xC0 | (cp >> 6));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[(*n)++] = (char)(0xE0 | (cp >> 12));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[(*n)++] = (char)(0xF0 | (cp >> 18));
        out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    }
}

static unsigned Read4Hex(Parser *ps) {
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int d = ps->p < ps->end ? HexDigit(*ps->p) : -1;
        if (d < 0) {
            Fail(ps, "bad \\u escape");
            return 0;
        }
        v = v * 16 + (unsigned)d;
        ps->p++;
    }
    return v;
}

static char *ParseString(Parser *ps) {
    const char *q;
    char *out;
    size_t n = 0;

    ps->p++;   /* the opening quote */
    for (q = ps->p; q < ps->end && *q != '"'; q++) {
        if (*q == '\\') {
            q++;
        }
    }
    if (q >= ps->end) {
        Fail(ps, "unterminated string");
        return NULL;
    }
    /* escapes never grow the text: \uXXXX (6 bytes) is at most 4 bytes of UTF-8 */
    out = SDL_malloc((size_t)(q - ps->p) + 1);
    if (out == NULL) {
        Fail(ps, "out of memory");
        return NULL;
    }
    while (ps->p < ps->end && *ps->p != '"') {
        char c = *ps->p++;
        if ((unsigned char)c < 0x20) {
            Fail(ps, "control character in a string");
            break;
        }
        if (c != '\\') {
            out[n++] = c;
            continue;
        }
        c = *ps->p++;
        switch (c) {
        case '"': out[n++] = '"'; break;
        case '\\': out[n++] = '\\'; break;
        case '/': out[n++] = '/'; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case 'u': {
            unsigned cp = Read4Hex(ps);
            if (cp >= 0xD800 && cp < 0xDC00 && ps->p + 1 < ps->end && ps->p[0] == '\\' && ps->p[1] == 'u') {
                unsigned lo;
                ps->p += 2;
                lo = Read4Hex(ps);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
            }
            PutUtf8(out, &n, cp);
            break;
        }
        default:
            Fail(ps, "bad escape");
            break;
        }
        if (ps->failed) {
            break;
        }
    }
    out[n] = 0;
    if (ps->failed) {
        SDL_free(out);
        return NULL;
    }
    ps->p++;   /* the closing quote */
    return out;
}

static JsonNode *ParseValue(Parser *ps);

static JsonNode *ParseContainer(Parser *ps, int object) {
    JsonNode *node = NewNode(object ? JSON_OBJECT : JSON_ARRAY);
    JsonNode **tail;
    char close = object ? '}' : ']';

    if (node == NULL) {
        Fail(ps, "out of memory");
        return NULL;
    }
    if (++ps->depth > 64) {
        Fail(ps, "nested too deep");
        return node;
    }
    tail = &node->child;
    ps->p++;
    for (;;) {
        char *key = NULL;
        JsonNode *v;

        SkipSpace(ps);
        if (ps->failed || ps->p >= ps->end) {
            Fail(ps, object ? "unterminated object" : "unterminated array");
            break;
        }
        if (*ps->p == close) {
            ps->p++;
            break;
        }
        if (object) {
            if (*ps->p != '"') {
                Fail(ps, "expected a member name");
                break;
            }
            key = ParseString(ps);
            if (key == NULL) {
                break;
            }
            SkipSpace(ps);
            if (ps->p >= ps->end || *ps->p != ':') {
                SDL_free(key);
                Fail(ps, "expected ':'");
                break;
            }
            ps->p++;
        }
        v = ParseValue(ps);
        if (v == NULL) {
            SDL_free(key);
            break;
        }
        v->key = key;
        *tail = v;
        tail = &v->next;
        node->count++;
        SkipSpace(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;          /* a trailing comma before the close is fine */
        } else if (ps->p < ps->end && *ps->p != close) {
            Fail(ps, object ? "expected ',' or '}'" : "expected ',' or ']'");
            break;
        }
    }
    ps->depth--;
    return node;
}

static JsonNode *ParseValue(Parser *ps) {
    JsonNode *node;

    SkipSpace(ps);
    if (ps->failed) {
        return NULL;
    }
    if (ps->p >= ps->end) {
        Fail(ps, "expected a value");
        return NULL;
    }
    switch (*ps->p) {
    case '{':
        return ParseContainer(ps, 1);
    case '[':
        return ParseContainer(ps, 0);
    case '"':
        node = NewNode(JSON_STRING);
        if (node != NULL) {
            node->string = ParseString(ps);
        }
        return node;
    }
    if (ps->end - ps->p >= 4 && SDL_memcmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        node = NewNode(JSON_BOOL);
        if (node != NULL) node->boolean = 1;
        return node;
    }
    if (ps->end - ps->p >= 5 && SDL_memcmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        return NewNode(JSON_BOOL);
    }
    if (ps->end - ps->p >= 4 && SDL_memcmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        return NewNode(JSON_NULL);
    }
    if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) {
        char tmp[64];
        const char *q = ps->p;
        size_t n;
        int integer = 1;

        while (q < ps->end && (SDL_strchr("+-0123456789.eE", *q) != NULL)) {
            if (*q == '.' || *q == 'e' || *q == 'E') {
                integer = 0;
            }
            q++;
        }
        n = (size_t)(q - ps->p);
        if (n >= sizeof(tmp)) {
            Fail(ps, "number too long");
            return NULL;
        }
        SDL_memcpy(tmp, ps->p, n);
        tmp[n] = 0;
        node = NewNode(JSON_NUMBER);
        if (node == NULL) {
            Fail(ps, "out of memory");
            return NULL;
        }
        {
            char *e = NULL;
            node->number = SDL_strtod(tmp, &e);
            if (e == tmp || *e != 0) {
                Fail(ps, "bad number");
            }
        }
        if (integer) {
            node->integer = SDL_strtoll(tmp, NULL, 10);
            node->isInteger = 1;
        } else {
            node->integer = (long long)node->number;
        }
        ps->p = q;
        return node;
    }
    Fail(ps, "unexpected character");
    return NULL;
}

JsonNode *Json_Parse(const char *text, size_t len, char *err, size_t errSize) {
    Parser ps;
    JsonNode *root;

    SDL_zero(ps);
    ps.p = ps.start = text;
    ps.end = text + len;
    ps.err = err;
    ps.errSize = errSize;
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        ps.p += 3;   /* a BOM from an editor */
    }
    root = ParseValue(&ps);
    SkipSpace(&ps);
    if (!ps.failed && ps.p != ps.end) {
        Fail(&ps, "text after the value");
    }
    if (ps.failed) {
        Json_Free(root);
        return NULL;
    }
    return root;
}

void Json_Free(JsonNode *node) {
    while (node != NULL) {
        JsonNode *next = node->next;
        Json_Free(node->child);
        SDL_free(node->key);
        SDL_free(node->string);
        SDL_free(node);
        node = next;
    }
}

const JsonNode *Json_Get(const JsonNode *obj, const char *key) {
    const JsonNode *n;
    if (obj == NULL || obj->type != JSON_OBJECT) {
        return NULL;
    }
    for (n = obj->child; n != NULL; n = n->next) {
        if (SDL_strcmp(n->key, key) == 0) {
            return n;
        }
    }
    return NULL;
}

const JsonNode *Json_At(const JsonNode *arr, int i) {
    const JsonNode *n;
    if (arr == NULL || arr->type != JSON_ARRAY || i < 0) {
        return NULL;
    }
    for (n = arr->child; n != NULL && i > 0; n = n->next) {
        i--;
    }
    return n;
}

long long Json_GetInt(const JsonNode *obj, const char *key, long long fallback) {
    const JsonNode *n = Json_Get(obj, key);
    return n != NULL && n->type == JSON_NUMBER ? n->integer : fallback;
}

double Json_GetNumber(const JsonNode *obj, const char *key, double fallback) {
    const JsonNode *n = Json_Get(obj, key);
    return n != NULL && n->type == JSON_NUMBER ? n->number : fallback;
}

const char *Json_GetString(const JsonNode *obj, const char *key, const char *fallback) {
    const JsonNode *n = Json_Get(obj, key);
    return n != NULL && n->type == JSON_STRING ? n->string : fallback;
}

int Json_GetBool(const JsonNode *obj, const char *key, int fallback) {
    const JsonNode *n = Json_Get(obj, key);
    return n != NULL && n->type == JSON_BOOL ? n->boolean : fallback;
}

int Json_HexDecode(const char *s, void *out, size_t size) {
    unsigned char *o = out;
    size_t i;

    if (s == NULL || SDL_strlen(s) != size * 2) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        int hi = HexDigit(s[2 * i]), lo = HexDigit(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        o[i] = (unsigned char)(hi * 16 + lo);
    }
    return 1;
}

JsonNode *Json_Clone(const JsonNode *node) {
    JsonNode *n;
    const JsonNode *c;

    if (node == NULL || (n = NewNode(node->type)) == NULL) {
        return NULL;
    }
    n->boolean = node->boolean;
    n->number = node->number;
    n->integer = node->integer;
    n->isInteger = node->isInteger;
    n->key = node->key != NULL ? SDL_strdup(node->key) : NULL;
    n->string = node->string != NULL ? SDL_strdup(node->string) : NULL;
    for (c = node->child; c != NULL; c = c->next) {
        Json_Append(n, Json_Clone(c), NULL);
    }
    return n;
}

JsonNode *Json_NewObject(void) {
    return NewNode(JSON_OBJECT);
}

void Json_Append(JsonNode *container, JsonNode *child, const char *key) {
    JsonNode **tail;

    if (container == NULL || child == NULL) {
        Json_Free(child);
        return;
    }
    if (key != NULL) {
        SDL_free(child->key);
        child->key = SDL_strdup(key);
    }
    for (tail = &container->child; *tail != NULL; tail = &(*tail)->next) {
    }
    child->next = NULL;
    *tail = child;
    container->count++;
}

JsonNode *Json_ObjectMember(JsonNode *obj, const char *key) {
    JsonNode *n = (JsonNode *)Json_Get(obj, key);

    if (n == NULL && obj != NULL && (n = Json_NewObject()) != NULL) {
        Json_Append(obj, n, key);
    }
    return n;
}

/* ---- writer ------------------------------------------------------------ */

static void Put(JsonWriter *w, const char *s, size_t n) {
    if (w->failed) {
        return;
    }
    if (w->len + n + 1 > w->cap) {
        size_t cap = w->cap ? w->cap : 4096;
        char *b;
        while (w->len + n + 1 > cap) {
            cap *= 2;
        }
        b = SDL_realloc(w->buf, cap);
        if (b == NULL) {
            w->failed = 1;
            return;
        }
        w->buf = b;
        w->cap = cap;
    }
    SDL_memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = 0;
}

static void PutS(JsonWriter *w, const char *s) {
    Put(w, s, SDL_strlen(s));
}

static void Indent(JsonWriter *w, int depth) {
    int i;
    Put(w, "\n", 1);
    for (i = 0; i < depth; i++) {
        Put(w, "  ", 2);
    }
}

static void PutQuoted(JsonWriter *w, const char *s) {
    Put(w, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            char e[2] = { '\\', (char)c };
            Put(w, e, 2);
        } else if (c < 0x20) {
            char e[8];
            SDL_snprintf(e, sizeof(e), "\\u%04x", c);
            PutS(w, e);
        } else {
            Put(w, (const char *)&c, 1);
        }
    }
    Put(w, "\"", 1);
}

/* Separator, indent and key ahead of a value. */
static void Element(JsonWriter *w, const char *key) {
    int d = w->depth;

    if (d > 0) {
        if (w->count[d] == 1) {
            Put(w, ",", 1);
        }
        if (w->inlined[d]) {
            if (w->count[d] != 0) {
                Put(w, " ", 1);
            }
        } else {
            Indent(w, d);
        }
        w->count[d] = 1;
    }
    if (key != NULL) {
        PutQuoted(w, key);
        Put(w, ": ", 2);
    }
}

void Jw_Init(JsonWriter *w) {
    SDL_zerop(w);
}

void Jw_Free(JsonWriter *w) {
    SDL_free(w->buf);
    SDL_zerop(w);
}

void Jw_Raw(JsonWriter *w, const char *text) {
    PutS(w, text);
}

void Jw_Comment(JsonWriter *w, const char *text) {
    int d = w->depth;

    if (d > 0 && w->count[d] == 1) {
        Put(w, ",", 1);
        w->count[d] = 2;     /* the comma is written: the next element adds none */
    }
    if (d > 0 || w->len > 0) {
        Indent(w, d);
    }
    PutS(w, "// ");
    PutS(w, text);
    if (d == 0) {
        /* before the root value: it starts on the next line */
        Put(w, "\n", 1);
    }
}

static void Begin(JsonWriter *w, const char *key, int inlined, const char *open) {
    Element(w, key);
    PutS(w, open);
    if (w->depth + 1 >= (int)sizeof(w->count)) {
        w->failed = 1;
        return;
    }
    w->depth++;
    w->count[w->depth] = 0;
    w->inlined[w->depth] = (unsigned char)(inlined || (w->depth > 1 && w->inlined[w->depth - 1]));
}

static void End(JsonWriter *w, const char *close) {
    int d = w->depth;
    if (d <= 0) {
        w->failed = 1;
        return;
    }
    if (!w->inlined[d] && w->count[d] != 0) {
        Indent(w, d - 1);
    }
    w->depth--;
    PutS(w, close);
}

void Jw_BeginObject(JsonWriter *w, const char *key, int inlined) { Begin(w, key, inlined, "{"); }
void Jw_EndObject(JsonWriter *w) { End(w, "}"); }
void Jw_BeginArray(JsonWriter *w, const char *key, int inlined) { Begin(w, key, inlined, "["); }
void Jw_EndArray(JsonWriter *w) { End(w, "]"); }

void Jw_Int(JsonWriter *w, const char *key, long long v) {
    char s[32];
    Element(w, key);
    SDL_snprintf(s, sizeof(s), "%lld", v);
    PutS(w, s);
}

void Jw_Float(JsonWriter *w, const char *key, float v) {
    char s[40];
    Element(w, key);
    if (isfinite(v)) {
        /* 9 significant digits read back to the same float */
        SDL_snprintf(s, sizeof(s), "%.9g", (double)v);
        PutS(w, s);
    } else {
        /* JSON has no NaN or infinity: the bits, as a string */
        unsigned int bits;
        SDL_memcpy(&bits, &v, 4);
        SDL_snprintf(s, sizeof(s), "\"0x%08X\"", bits);
        PutS(w, s);
    }
}

void Jw_Bool(JsonWriter *w, const char *key, int v) {
    Element(w, key);
    PutS(w, v ? "true" : "false");
}

void Jw_String(JsonWriter *w, const char *key, const char *s) {
    Element(w, key);
    PutQuoted(w, s);
}

void Jw_Hex(JsonWriter *w, const char *key, const void *data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    const unsigned char *b = data;
    size_t i;

    Element(w, key);
    Put(w, "\"", 1);
    for (i = 0; i < size; i++) {
        char h[2] = { digits[b[i] >> 4], digits[b[i] & 15] };
        Put(w, h, 2);
    }
    Put(w, "\"", 1);
}

void Jw_Node(JsonWriter *w, const char *key, const JsonNode *node) {
    const JsonNode *c;

    switch (node->type) {
    case JSON_NULL:
        Element(w, key);
        PutS(w, "null");
        break;
    case JSON_BOOL:
        Jw_Bool(w, key, node->boolean);
        break;
    case JSON_NUMBER:
        if (node->isInteger) {
            Jw_Int(w, key, node->integer);
        } else {
            char s[40];
            Element(w, key);
            SDL_snprintf(s, sizeof(s), "%.17g", node->number);
            PutS(w, s);
        }
        break;
    case JSON_STRING:
        Jw_String(w, key, node->string);
        break;
    case JSON_ARRAY:
        Jw_BeginArray(w, key, 1);
        for (c = node->child; c != NULL; c = c->next) {
            Jw_Node(w, NULL, c);
        }
        Jw_EndArray(w);
        break;
    case JSON_OBJECT:
        Jw_BeginObject(w, key, 1);
        for (c = node->child; c != NULL; c = c->next) {
            Jw_Node(w, c->key, c);
        }
        Jw_EndObject(w);
        break;
    }
}

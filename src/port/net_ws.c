/* The WebSocket protocol (RFC 6455) as far as the transport needs it: SHA-1 and
 * base64 for the handshake, the handshake's HTTP, and frames. No sockets here.
 * Everything parsed comes from the network: it is checked before it is used. */
#include <SDL3/SDL.h>

#include "net_internal.h"

/* ---- SHA-1: the handshake requires it; it protects nothing here ---------- */

static u32 Rol(u32 x, int n) {
    return x << n | x >> (32 - n);
}

static void Sha1Block(u32 h[5], const u8 *p) {
    u32 w[80];
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (u32)p[i * 4] << 24 | (u32)p[i * 4 + 1] << 16 | (u32)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    }
    for (; i < 80; i++) {
        w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    for (i = 0; i < 80; i++) {
        u32 f, k, t;

        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        t = Rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void Ws_Sha1(const void *data, u32 size, u8 out[20]) {
    u32 h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    const u8 *p = data;
    u32 left = size;
    u8 last[128];
    u32 n;
    u64 bits = (u64)size * 8;
    int i;

    for (; left >= 64; left -= 64, p += 64) {
        Sha1Block(h, p);
    }
    SDL_zeroa(last);
    if (left != 0) {
        SDL_memcpy(last, p, left);
    }
    last[left] = 0x80;
    n = left + 9 <= 64 ? 64 : 128;
    for (i = 0; i < 8; i++) {
        last[n - 1 - i] = (u8)(bits >> (i * 8));
    }
    Sha1Block(h, last);
    if (n == 128) {
        Sha1Block(h, last + 64);
    }
    for (i = 0; i < 5; i++) {
        out[i * 4] = (u8)(h[i] >> 24);
        out[i * 4 + 1] = (u8)(h[i] >> 16);
        out[i * 4 + 2] = (u8)(h[i] >> 8);
        out[i * 4 + 3] = (u8)h[i];
    }
}

void Ws_Base64(const void *data, u32 size, char *out) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const u8 *p = data;

    for (; size >= 3; size -= 3, p += 3) {
        *out++ = digits[p[0] >> 2];
        *out++ = digits[(p[0] & 3) << 4 | p[1] >> 4];
        *out++ = digits[(p[1] & 15) << 2 | p[2] >> 6];
        *out++ = digits[p[2] & 63];
    }
    if (size != 0) {
        *out++ = digits[p[0] >> 2];
        if (size == 1) {
            *out++ = digits[(p[0] & 3) << 4];
            *out++ = '=';
        } else {
            *out++ = digits[(p[0] & 3) << 4 | p[1] >> 4];
            *out++ = digits[(p[1] & 15) << 2];
        }
        *out++ = '=';
    }
    *out = 0;
}

void Ws_AcceptKey(const char *key, char out[WS_ACCEPT_LEN + 1]) {
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char joined[WS_KEY_MAX + sizeof(guid)];
    u8 digest[20];

    SDL_strlcpy(joined, key, WS_KEY_MAX);
    SDL_strlcat(joined, guid, sizeof(joined));
    Ws_Sha1(joined, (u32)SDL_strlen(joined), digest);
    Ws_Base64(digest, sizeof(digest), out);
}

/* ---- frames -------------------------------------------------------------- */

/* Written with a counter, not as key[i & 3]: Apple clang 21 at -O2 vectorized that
 * form in Ws_BuildFrame into 16-byte reads past the key, giving wrong bytes from
 * the fourth on. out may be in. */
static void MaskBytes(u8 *out, const u8 *in, u32 size, const u8 *mask) {
    u8 key[4];
    u32 k = 0;
    u32 i;

    SDL_memcpy(key, mask, 4);
    for (i = 0; i < size; i++) {
        out[i] = in[i] ^ key[k];
        k = (k + 1) & 3;
    }
}

s32 Ws_ParseFrame(u8 *buf, u32 len, int expectMasked, u32 maxPayload, WsFrame *frame) {
    u32 header = 2;
    u64 size;
    u8 opcode;
    int masked;

    if (len < 2) {
        return 0;
    }
    opcode = buf[0] & 0x0F;
    masked = (buf[1] & 0x80) != 0;
    size = buf[1] & 0x7F;
    if ((buf[0] & 0x70) != 0 || masked != expectMasked) {
        return -1;              /* extension bits, none negotiated */
    }
    switch (opcode) {
    case WS_OP_CONTINUATION:
    case WS_OP_TEXT:
    case WS_OP_BINARY:
        break;
    case WS_OP_CLOSE:
    case WS_OP_PING:
    case WS_OP_PONG:
        if (!(buf[0] & 0x80) || size > 125) {
            return -1;          /* control frames are short and whole */
        }
        break;
    default:
        return -1;
    }
    if (size == 126) {
        if (len < 4) {
            return 0;
        }
        size = (u32)buf[2] << 8 | buf[3];
        header = 4;
    } else if (size == 127) {
        int i;

        if (len < 10) {
            return 0;
        }
        size = 0;
        for (i = 0; i < 8; i++) {
            size = size << 8 | buf[2 + i];
        }
        header = 10;
    }
    if (size > maxPayload) {
        return -1;
    }
    if (masked) {
        header += 4;
    }
    if (len < header || len - header < size) {
        return 0;
    }
    frame->opcode = opcode;
    frame->fin = (buf[0] & 0x80) != 0;
    frame->payload = buf + header;
    frame->size = (u32)size;
    if (masked) {
        MaskBytes(frame->payload, frame->payload, frame->size, buf + header - 4);
    }
    return (s32)(header + frame->size);
}

u32 Ws_BuildFrame(u8 *out, u8 opcode, const void *data, u32 size, const u8 *mask) {
    u8 maskBit = mask != NULL ? 0x80 : 0;
    u32 header = 2;

    out[0] = 0x80 | opcode;
    if (size < 126) {
        out[1] = maskBit | (u8)size;
    } else if (size < 65536) {
        out[1] = maskBit | 126;
        out[2] = (u8)(size >> 8);
        out[3] = (u8)size;
        header = 4;
    } else {
        int i;

        out[1] = maskBit | 127;
        for (i = 0; i < 8; i++) {
            out[2 + i] = i < 4 ? 0 : (u8)(size >> ((7 - i) * 8));
        }
        header = 10;
    }
    if (mask != NULL) {
        SDL_memcpy(out + header, mask, 4);
        header += 4;
        if (size != 0) {
            MaskBytes(out + header, data, size, mask);
        }
    } else if (size != 0) {
        SDL_memcpy(out + header, data, size);
    }
    return header + size;
}

/* ---- the handshake ------------------------------------------------------- */

u32 Ws_HeadLength(const u8 *buf, u32 len) {
    u32 i;

    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i + 4;
        }
    }
    return 0;
}

/* Copies the value of a header (case-insensitive name, trimmed value) to out;
 * 0 if it is missing or doesn't fit. */
static int HeaderValue(const char *head, const char *name, char *out, size_t cap) {
    size_t nameLen = SDL_strlen(name);
    const char *line = SDL_strstr(head, "\r\n");

    while (line != NULL) {
        const char *next;

        line += 2;
        next = SDL_strstr(line, "\r\n");
        if (next == NULL) {
            return 0;
        }
        if ((size_t)(next - line) > nameLen && line[nameLen] == ':' && SDL_strncasecmp(line, name, nameLen) == 0) {
            const char *v = line + nameLen + 1;
            const char *end = next;
            size_t n;

            while (v < end && (*v == ' ' || *v == '\t')) {
                v++;
            }
            while (end > v && (end[-1] == ' ' || end[-1] == '\t')) {
                end--;
            }
            n = (size_t)(end - v);
            if (n >= cap) {
                return 0;
            }
            SDL_memcpy(out, v, n);
            out[n] = 0;
            return 1;
        }
        line = next;
    }
    return 0;
}

int Ws_ParseRequest(const char *head, char key[WS_KEY_MAX]) {
    char value[256];
    size_t pathLen = sizeof(WS_PATH) - 1;

    if (SDL_strncmp(head, "GET " WS_PATH, 4 + pathLen) != 0 ||
        (head[4 + pathLen] != ' ' && head[4 + pathLen] != '?' && head[4 + pathLen] != '/')) {
        return 0;
    }
    if (!HeaderValue(head, "Upgrade", value, sizeof(value)) || SDL_strcasecmp(value, "websocket") != 0) {
        return 0;
    }
    /* a list: proxies may send "keep-alive, Upgrade" */
    if (!HeaderValue(head, "Connection", value, sizeof(value)) || SDL_strcasestr(value, "upgrade") == NULL) {
        return 0;
    }
    if (!HeaderValue(head, "Sec-WebSocket-Version", value, sizeof(value)) || SDL_strcmp(value, "13") != 0) {
        return 0;
    }
    return HeaderValue(head, "Sec-WebSocket-Key", key, WS_KEY_MAX) && key[0] != 0;
}

int Ws_CheckResponse(const char *head, const char *key) {
    char value[WS_ACCEPT_LEN + 8];
    char expected[WS_ACCEPT_LEN + 1];

    if (SDL_strncmp(head, "HTTP/1.1 101", 12) != 0 && SDL_strncmp(head, "HTTP/1.0 101", 12) != 0) {
        return 0;
    }
    if (!HeaderValue(head, "Sec-WebSocket-Accept", value, sizeof(value))) {
        return 0;
    }
    Ws_AcceptKey(key, expected);
    return SDL_strcmp(value, expected) == 0;
}

/* ---- what people type ---------------------------------------------------- */

#define QUICK_TUNNEL_DOMAIN ".trycloudflare.com"

int Ws_ParseAddress(const char *text, NetAddress *out) {
    const char *p = text, *end, *hostEnd, *portText = NULL, *path;
    size_t n, i;
    int scheme = 0;

    SDL_zerop(out);
    while (*p == ' ') {
        p++;
    }
    end = p + SDL_strlen(p);
    while (end > p && end[-1] == ' ') {
        end--;
    }
    if (SDL_strncasecmp(p, "wss://", 6) == 0 || SDL_strncasecmp(p, "https://", 8) == 0) {
        p = SDL_strchr(p, '/') + 2;
        out->tls = 1;
        scheme = 1;
    } else if (SDL_strncasecmp(p, "ws://", 5) == 0 || SDL_strncasecmp(p, "http://", 7) == 0) {
        p = SDL_strchr(p, '/') + 2;
        scheme = 1;
    } else if (SDL_strstr(p, "://") != NULL) {
        return 0;
    }
    /* /CODE: what's after the first slash */
    path = p;
    while (path < end && *path != '/') {
        path++;
    }
    if (path < end) {
        const char *code = path + 1;

        n = (size_t)(end - code);
        if (n >= sizeof(out->code)) {
            return 0;
        }
        for (i = 0; i < n; i++) {
            out->code[i] = (char)SDL_toupper((unsigned char)code[i]);
        }
        end = path;
    }
    if (*p == '[') {
        const char *close = p + 1;

        while (close < end && *close != ']') {
            close++;
        }
        if (close >= end) {
            return 0;
        }
        hostEnd = close;
        p++;
        if (close + 1 < end) {
            if (close[1] != ':') {
                return 0;
            }
            portText = close + 2;
        }
    } else {
        const char *colon = p, *second = NULL;

        while (colon < end && *colon != ':') {
            colon++;
        }
        if (colon < end) {
            second = colon + 1;
            while (second < end && *second != ':') {
                second++;
            }
        }
        if (colon < end && second >= end) {
            hostEnd = colon;
            portText = colon + 1;
        } else {
            hostEnd = end;      /* no colon, or a bare IPv6 address */
        }
    }
    n = (size_t)(hostEnd - p);
    if (n == 0 || n >= sizeof(out->host) - sizeof(QUICK_TUNNEL_DOMAIN)) {
        return 0;
    }
    SDL_memcpy(out->host, p, n);
    out->host[n] = 0;
    if (portText != NULL) {
        char *after;
        char digits[8];
        size_t len = (size_t)(end - portText);
        long port;

        if (len == 0 || len >= sizeof(digits)) {
            return 0;
        }
        SDL_memcpy(digits, portText, len);
        digits[len] = 0;
        port = SDL_strtol(digits, &after, 10);
        if (*after != 0 || port < 1 || port > 65535) {
            return 0;
        }
        out->port = (u16)port;
    }
    if (!scheme) {
        /* word-word-word-word: what a quick tunnel is called */
        int hyphens = 0, plain = portText == NULL;

        for (i = 0; i < n; i++) {
            if (out->host[i] == '-') {
                hyphens++;
            } else if (out->host[i] == '.' || out->host[i] == ':') {
                plain = 0;
            }
        }
        if (plain && hyphens >= 2) {
            SDL_strlcat(out->host, QUICK_TUNNEL_DOMAIN, sizeof(out->host));
            n = SDL_strlen(out->host);
        }
        if (n > sizeof(QUICK_TUNNEL_DOMAIN) - 1 &&
            SDL_strcasecmp(out->host + n - (sizeof(QUICK_TUNNEL_DOMAIN) - 1), QUICK_TUNNEL_DOMAIN) == 0) {
            out->tls = 1;
        }
    }
    if (out->port == 0) {
        out->port = out->tls ? 443 : scheme ? 80 : NET_DEFAULT_PORT;
    }
    return 1;
}

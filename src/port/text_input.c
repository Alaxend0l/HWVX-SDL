/* Typing into a menu, for the online address: the system's text input (its
 * on-screen keyboard on Android), Backspace and the clipboard, into a buffer the
 * caller owns. And the one small setting that goes with it, the last address. */
#include "port_internal.h"

static char *s_buf;
static size_t s_cap;

/* What an address can hold; anything else would only have to fail later. */
static int AllowedChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == ':' || c == '-' || c == '_' || c == '[' || c == ']' || c == '/';
}

static void Append(const char *text) {
    size_t len = SDL_strlen(s_buf);

    for (; *text != 0 && len + 1 < s_cap; text++) {
        if (AllowedChar(*text)) {
            s_buf[len++] = *text;
        }
    }
    s_buf[len] = 0;
}

void Port_TextInputStart(char *buf, size_t cap) {
    s_buf = buf;
    s_cap = cap;
    SDL_StartTextInput(g_port.window);
}

void Port_TextInputStop(void) {
    if (s_buf != NULL) {
        s_buf = NULL;
        SDL_StopTextInput(g_port.window);
    }
}

/* Replaces the buffer's text with the clipboard's; 0 if that holds no text. */
int Port_TextInputPaste(void) {
    char *text;
    int ok = 0;

    if (s_buf == NULL) {
        return 0;
    }
    text = SDL_GetClipboardText();
    if (text != NULL && *text != 0) {
        s_buf[0] = 0;
        Append(text);
        ok = 1;
    }
    SDL_free(text);
    return ok;
}

void Port_TextHandleEvent(const SDL_Event *ev) {
    if (s_buf == NULL) {
        return;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
        Append(ev->text.text);
    } else if (ev->type == SDL_EVENT_KEY_DOWN) {
        if (ev->key.key == SDLK_BACKSPACE) {
            size_t len = SDL_strlen(s_buf);

            if (len != 0) {
                s_buf[len - 1] = 0;
            }
        } else if (ev->key.key == SDLK_V && (ev->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
            Port_TextInputPaste();
        }
    }
}

static char *AddressPath(void) {
    char *pref = Port_PrefPath();
    char *path = NULL;

    if (pref != NULL) {
        SDL_asprintf(&path, "%sonline.cfg", pref);
        SDL_free(pref);
    }
    return path;
}

/* HWVX_ONLINE_ADDRESS (testing), else the address last connected to. */
void Port_LoadOnlineAddress(char *buf, size_t cap) {
    const char *env = SDL_getenv("HWVX_ONLINE_ADDRESS");
    char *path;

    buf[0] = 0;
    if (env != NULL) {
        SDL_strlcpy(buf, env, cap);
        return;
    }
    path = AddressPath();
    if (path != NULL) {
        size_t size = 0;
        char *data = SDL_LoadFile(path, &size);

        if (data != NULL) {
            size_t i, n = 0;

            for (i = 0; i < size && n + 1 < cap && AllowedChar(data[i]); i++) {
                buf[n++] = data[i];
            }
            buf[n] = 0;
            SDL_free(data);
        }
        SDL_free(path);
    }
}

void Port_SaveOnlineAddress(const char *address) {
    char *path = SDL_getenv("HWVX_ONLINE_ADDRESS") == NULL ? AddressPath() : NULL;

    if (path != NULL) {
        if (!SDL_SaveFile(path, address, SDL_strlen(address))) {
            Port_Log("online.cfg: %s", SDL_GetError());
        }
        SDL_free(path);
    }
}

void Port_TextCopy(const char *text) {
    if (!SDL_SetClipboardText(text)) {
        Port_Log("clipboard: %s", SDL_GetError());
    }
}

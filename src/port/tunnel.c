/* Hosting through a Cloudflare quick tunnel: runs the player's own cloudflared and reads
 * the address it was given from what it prints. It carries connections to the session's
 * local WebSocket port (src/port/net.c), and ends TLS itself. The program isn't shipped:
 * it's looked for next to the game, then where installers put it. Not on Android, which
 * can't run a program this way; players there can still join. */
#include "port_internal.h"

#ifdef SDL_PLATFORM_ANDROID

int Port_TunnelAvailable(void) { return 0; }
int Port_TunnelStart(u16 port) { (void)port; return 0; }
int Port_TunnelPoll(char *name, size_t size) { (void)name; (void)size; return -1; }
void Port_TunnelStop(void) {}

#else

/* Looked up too soon, the new name can fail to resolve for over a minute: resolvers
 * remember the failure. Measured; 15 s after registering has been enough. */
#define TUNNEL_DNS_WAIT_MS 15000
#define TUNNEL_DOMAIN ".trycloudflare.com"

static SDL_Process *s_process;
static SDL_Thread *s_reader;
static SDL_AtomicInt s_stopReader;
static SDL_AtomicInt s_failed;
static SDL_Mutex *s_lock;
static char s_name[128];
static u64 s_registeredAt;

/* The name in https://NAME.trycloudflare.com, if this has it. */
static void FindName(const char *text) {
    const char *end = SDL_strstr(text, TUNNEL_DOMAIN);
    const char *start = end;

    if (end == NULL) {
        return;
    }
    while (start > text && (SDL_isalnum((unsigned char)start[-1]) || start[-1] == '-')) {
        start--;
    }
    /* cloudflared's own api.trycloudflare.com is in what it prints too */
    if (end - start >= 8 && (size_t)(end - start) < sizeof(s_name) && start - text >= 8 &&
        SDL_strncmp(start - 8, "https://", 8) == 0 && SDL_strchr(start, '-') != NULL && SDL_strchr(start, '-') < end) {
        SDL_LockMutex(s_lock);
        SDL_memcpy(s_name, start, (size_t)(end - start));
        s_name[end - start] = 0;
        SDL_UnlockMutex(s_lock);
    }
}

/* It must be read to the end whatever is going on, or it stops when its pipe fills. */
static int ReaderThread(void *arg) {
    SDL_IOStream *out = arg;
    char buf[2048];
    size_t have = 0;

    while (!SDL_GetAtomicInt(&s_stopReader)) {
        size_t got = SDL_ReadIO(out, buf + have, sizeof(buf) - 1 - have);

        if (got == 0) {
            if (SDL_GetIOStatus(out) != SDL_IO_STATUS_NOT_READY) {
                SDL_SetAtomicInt(&s_failed, 1);   /* it ended */
                return 0;
            }
            SDL_Delay(50);
            continue;
        }
        have += got;
        buf[have] = 0;
        for (;;) {
            char *line = SDL_strchr(buf, '\n');

            if (line == NULL) {
                break;
            }
            *line = 0;
            FindName(buf);
            if (SDL_strstr(buf, "Registered tunnel connection") != NULL) {
                SDL_LockMutex(s_lock);
                if (s_registeredAt == 0) {
                    s_registeredAt = SDL_GetTicks();
                }
                SDL_UnlockMutex(s_lock);
            }
            have -= (size_t)(line + 1 - buf);
            SDL_memmove(buf, line + 1, have + 1);
        }
        if (have >= sizeof(buf) - 1) {
            have = 0;           /* a line this long is nothing we're looking for */
        }
    }
    return 0;
}

int Port_TunnelAvailable(void) {
    return 1;
}

static SDL_Process *Launch(const char *program, u16 port) {
    char url[64];
    const char *args[] = {program, "tunnel", "--no-autoupdate", "--url", url, NULL};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Process *p;

    SDL_snprintf(url, sizeof(url), "http://127.0.0.1:%u", port);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
#ifdef _WIN32
    /* No console window. Only here: elsewhere SDL's background mode double-forks and can't
     * read, wait for or kill what it started by pid (and has no window to hide anyway). */
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    return p;
}

static int FileExists(const char *path) {
    return SDL_GetPathInfo(path, NULL);
}

/* Whichever exit the game takes, the tunnel doesn't stay up behind it. */
static void StopAtExit(void) {
    Port_TunnelStop();
}

int Port_TunnelStart(u16 port) {
#ifdef _WIN32
    static const char program[] = "cloudflared.exe";
    static const char *const places[] = {"C:\\Program Files (x86)\\cloudflared\\", "C:\\Program Files\\cloudflared\\"};
#else
    static const char program[] = "cloudflared";
    /* a program started from a desktop may have none of these on its PATH */
    static const char *const places[] = {"/opt/homebrew/bin/", "/usr/local/bin/", "/usr/bin/"};
#endif
    static int registered;
    const char *base = SDL_GetBasePath();
    char path[1024];
    u32 i;

    Port_TunnelStop();
    if (s_lock == NULL) {
        s_lock = SDL_CreateMutex();
    }
    if (!registered) {
        registered = 1;
        atexit(StopAtExit);
    }
    s_name[0] = 0;
    s_registeredAt = 0;
    SDL_SetAtomicInt(&s_failed, 0);
    SDL_SetAtomicInt(&s_stopReader, 0);
    if (base != NULL) {
        SDL_snprintf(path, sizeof(path), "%s%s", base, program);
        if (FileExists(path)) {
            s_process = Launch(path, port);
        }
    }
    for (i = 0; s_process == NULL && i < SDL_arraysize(places); i++) {
        SDL_snprintf(path, sizeof(path), "%s%s", places[i], program);
        if (FileExists(path)) {
            s_process = Launch(path, port);
        }
    }
    if (s_process == NULL) {
        s_process = Launch(program, port);      /* the PATH */
    }
    if (s_process == NULL) {
        Port_Log("tunnel: no %s found; https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/downloads/", program);
        return 0;
    }
    s_reader = SDL_CreateThread(ReaderThread, "tunnel", SDL_GetProcessOutput(s_process));
    if (s_reader == NULL) {
        Port_TunnelStop();
        return 0;
    }
    return 1;
}

int Port_TunnelPoll(char *name, size_t size) {
    int ready;

    if (s_process == NULL || SDL_GetAtomicInt(&s_failed)) {
        return -1;
    }
    SDL_LockMutex(s_lock);
    ready = s_name[0] != 0 && s_registeredAt != 0 && SDL_GetTicks() - s_registeredAt >= TUNNEL_DNS_WAIT_MS;
    if (ready) {
        SDL_strlcpy(name, s_name, size);
    }
    SDL_UnlockMutex(s_lock);
    return ready;
}

void Port_TunnelStop(void) {
    if (s_process == NULL) {
        return;
    }
    SDL_SetAtomicInt(&s_stopReader, 1);
    SDL_KillProcess(s_process, true);
    if (s_reader != NULL) {
        SDL_WaitThread(s_reader, NULL);
        s_reader = NULL;
    }
    SDL_WaitProcess(s_process, true, NULL);
    SDL_DestroyProcess(s_process);
    s_process = NULL;
}

#endif

/*
 * ask.c - "ask" command: sends a question to the RAG server and delivers
 * the answer back to the player asynchronously.
 *
 * The MUD is single-threaded, and the RAG /ask endpoint can take several
 * seconds per query. A detached pthread does the HTTP call; the main game
 * loop drains a reply queue once per pulse via ask_pump().
 */
#if defined(macintosh)
#include <types.h>
#else
#include <sys/types.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "merc.h"

#define ASK_WRAP_COLS       78
#define ASK_MAX_QUESTION    512
#define ASK_MAX_REPLY       8192
#define ASK_HTTP_TIMEOUT_S  120
#define ASK_THINK_INTERVAL  10    /* seconds between "thinking..." nudges */

typedef struct ask_pending ASK_PENDING;
struct ask_pending
{
    ASK_PENDING *next;
    CHAR_DATA   *ch;       /* target; validated against char_list before use */
    char        *reply;    /* malloc'd, or NULL if the request failed silently */
    time_t       next_tick; /* ask_active only: when to send next "thinking" */
};

static pthread_mutex_t ask_mutex   = PTHREAD_MUTEX_INITIALIZER;
static ASK_PENDING    *ask_replies = NULL;   /* answers ready to deliver */
static ASK_PENDING    *ask_active  = NULL;   /* in-flight, one entry per asker */

typedef struct
{
    CHAR_DATA *ch;
    char       question[ASK_MAX_QUESTION];
    char       host[128];
    int        port;
} ASK_JOB;

/* -------- small helpers -------- */

static bool ch_still_alive(CHAR_DATA *ch)
{
    CHAR_DATA *c;
    for (c = char_list; c != NULL; c = c->next)
        if (c == ch) return TRUE;
    return FALSE;
}

static bool ask_is_pending(CHAR_DATA *ch)
{
    ASK_PENDING *p;
    bool found = FALSE;
    pthread_mutex_lock(&ask_mutex);
    for (p = ask_active; p != NULL; p = p->next)
        if (p->ch == ch) { found = TRUE; break; }
    pthread_mutex_unlock(&ask_mutex);
    return found;
}

static void ask_mark_active(CHAR_DATA *ch)
{
    ASK_PENDING *p = malloc(sizeof(*p));
    if (p == NULL) return;
    p->ch        = ch;
    p->reply     = NULL;
    p->next_tick = current_time + ASK_THINK_INTERVAL;
    pthread_mutex_lock(&ask_mutex);
    p->next = ask_active;
    ask_active = p;
    pthread_mutex_unlock(&ask_mutex);
}

static void ask_clear_active(CHAR_DATA *ch)
{
    ASK_PENDING **pp, *dead;
    pthread_mutex_lock(&ask_mutex);
    pp = &ask_active;
    while (*pp != NULL)
    {
        if ((*pp)->ch == ch)
        {
            dead = *pp;
            *pp = dead->next;
            free(dead);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&ask_mutex);
}

static void ask_enqueue_reply(CHAR_DATA *ch, char *reply)
{
    ASK_PENDING *p = malloc(sizeof(*p));
    if (p == NULL) { free(reply); return; }
    p->ch        = ch;
    p->reply     = reply;
    p->next_tick = 0;
    pthread_mutex_lock(&ask_mutex);
    p->next = ask_replies;
    ask_replies = p;
    pthread_mutex_unlock(&ask_mutex);
}

/* Wrap text to cols, preserving paragraph breaks. Caller frees result. */
static char *wrap_text(const char *src, int cols)
{
    size_t cap = strlen(src) * 2 + 64;
    char *out = malloc(cap);
    size_t out_len = 0;
    int col = 0;
    const char *p = src;

    if (out == NULL) return NULL;

    while (*p)
    {
        /* Preserve explicit newlines as paragraph breaks. */
        if (*p == '\n')
        {
            if (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); if (!out) return NULL; }
            out[out_len++] = '\n';
            out[out_len++] = '\r';
            col = 0;
            p++;
            continue;
        }
        if (isspace((unsigned char)*p))
        {
            p++;
            continue;
        }

        /* Measure next word. */
        const char *wstart = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t wlen = (size_t)(p - wstart);

        if (col > 0 && col + 1 + (int)wlen > cols)
        {
            if (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); if (!out) return NULL; }
            out[out_len++] = '\n';
            out[out_len++] = '\r';
            col = 0;
        }
        if (col > 0)
        {
            if (out_len + 1 >= cap) { cap *= 2; out = realloc(out, cap); if (!out) return NULL; }
            out[out_len++] = ' ';
            col++;
        }
        while (out_len + wlen + 3 >= cap) { cap *= 2; out = realloc(out, cap); if (!out) return NULL; }
        memcpy(out + out_len, wstart, wlen);
        out_len += wlen;
        col += (int)wlen;
    }

    if (out_len == 0 || out[out_len - 1] != '\r')
    {
        if (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); if (!out) return NULL; }
        out[out_len++] = '\n';
        out[out_len++] = '\r';
    }
    out[out_len] = '\0';
    return out;
}

/* -------- JSON: build request, parse "answer" from response -------- */

/* Emit a JSON string literal: quotes + backslash-escape control chars. */
static void json_escape_append(char *dst, size_t cap, size_t *len, const char *s)
{
    size_t i = *len;
    for (; *s && i + 7 < cap; s++)
    {
        unsigned char c = (unsigned char)*s;
        switch (c)
        {
            case '"':  dst[i++] = '\\'; dst[i++] = '"';  break;
            case '\\': dst[i++] = '\\'; dst[i++] = '\\'; break;
            case '\b': dst[i++] = '\\'; dst[i++] = 'b';  break;
            case '\f': dst[i++] = '\\'; dst[i++] = 'f';  break;
            case '\n': dst[i++] = '\\'; dst[i++] = 'n';  break;
            case '\r': dst[i++] = '\\'; dst[i++] = 'r';  break;
            case '\t': dst[i++] = '\\'; dst[i++] = 't';  break;
            default:
                if (c < 0x20)
                    i += snprintf(dst + i, cap - i, "\\u%04x", c);
                else
                    dst[i++] = (char)c;
        }
    }
    *len = i;
}

/*
 * Find the value of the "answer" field in a JSON response and return a
 * heap-allocated unescaped copy, or NULL if absent/null.
 *
 * The RAG server emits a flat top-level object, but "answer" may appear
 * inside "hits" values too. We scan for the top-level key by tracking
 * brace depth.
 */
static char *json_extract_answer(const char *body)
{
    const char *p = body;
    int depth = 0;
    const char *key = "\"answer\"";
    size_t klen = strlen(key);
    const char *value_start = NULL;

    while (*p)
    {
        if (*p == '"')
        {
            /* Skip over any string; we only care about keys at depth 1. */
            if (depth == 1 && strncmp(p, key, klen) == 0)
            {
                const char *q = p + klen;
                while (*q && isspace((unsigned char)*q)) q++;
                if (*q == ':')
                {
                    q++;
                    while (*q && isspace((unsigned char)*q)) q++;
                    value_start = q;
                    break;
                }
            }
            /* Skip the string literal, honouring escapes. */
            p++;
            while (*p && *p != '"')
            {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
            if (*p == '"') p++;
            continue;
        }
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        p++;
    }

    if (value_start == NULL) return NULL;
    if (strncmp(value_start, "null", 4) == 0) return NULL;
    if (*value_start != '"') return NULL;

    value_start++;  /* skip opening quote */
    size_t cap = 256;
    char *out = malloc(cap);
    size_t len = 0;
    if (out == NULL) return NULL;

    while (*value_start && *value_start != '"')
    {
        if (len + 8 >= cap) { cap *= 2; out = realloc(out, cap); if (!out) return NULL; }
        if (*value_start == '\\' && value_start[1])
        {
            char esc = value_start[1];
            value_start += 2;
            switch (esc)
            {
                case '"':  out[len++] = '"';  break;
                case '\\': out[len++] = '\\'; break;
                case '/':  out[len++] = '/';  break;
                case 'b':  out[len++] = '\b'; break;
                case 'f':  out[len++] = '\f'; break;
                case 'n':  out[len++] = '\n'; break;
                case 'r':  /* drop CRs, wrap_text re-inserts them */   break;
                case 't':  out[len++] = '\t'; break;
                case 'u':
                {
                    /* Handle \uXXXX for ASCII range only; strip others. */
                    if (isxdigit((unsigned char)value_start[0]) && isxdigit((unsigned char)value_start[1])
                     && isxdigit((unsigned char)value_start[2]) && isxdigit((unsigned char)value_start[3]))
                    {
                        char hex[5] = { value_start[0], value_start[1], value_start[2], value_start[3], 0 };
                        unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                        value_start += 4;
                        if (cp >= 0x20 && cp < 0x80) out[len++] = (char)cp;
                    }
                    break;
                }
                default:   out[len++] = esc; break;
            }
        }
        else
        {
            out[len++] = *value_start++;
        }
    }
    out[len] = '\0';
    return out;
}

/* -------- HTTP -------- */

static int tcp_connect(const char *host, int port)
{
    struct addrinfo hints, *res, *it;
    char port_s[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_s, sizeof(port_s), "%d", port);

    if (getaddrinfo(host, port_s, &hints, &res) != 0) return -1;
    for (it = res; it != NULL; it = it->ai_next)
    {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static bool write_all(int fd, const char *buf, size_t len)
{
    while (len > 0)
    {
        ssize_t n = send(fd, buf, len, 0);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR) continue;
            return FALSE;
        }
        buf += n;
        len -= (size_t)n;
    }
    return TRUE;
}

/* Read until the peer closes the connection. */
static char *read_all(int fd)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) return NULL;
    for (;;)
    {
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); if (!buf) return NULL; }
        ssize_t n = recv(fd, buf + len, cap - len - 1, 0);
        if (n == 0) break;
        if (n < 0)
        {
            if (errno == EINTR) continue;
            free(buf);
            return NULL;
        }
        len += (size_t)n;
    }
    buf[len] = '\0';
    return buf;
}

static char *http_body(char *response)
{
    char *sep = strstr(response, "\r\n\r\n");
    if (sep == NULL) return NULL;
    return sep + 4;
}

/* Worker thread entry. Takes ownership of the ASK_JOB. */
static void *ask_worker(void *arg)
{
    ASK_JOB *job = (ASK_JOB *)arg;
    char   *answer_raw = NULL;
    char   *answer_wrapped = NULL;

    /* Build JSON body. */
    char body[ASK_MAX_QUESTION + 128];
    size_t body_len = 0;
    body[0] = '\0';
    body_len = (size_t)snprintf(body, sizeof(body), "{\"question\":\"");
    json_escape_append(body, sizeof(body), &body_len, job->question);
    body_len += (size_t)snprintf(body + body_len, sizeof(body) - body_len,
                                 "\",\"k\":3,\"llm\":true}");

    /* Build HTTP request. */
    char request[ASK_MAX_QUESTION + 512];
    int req_len = snprintf(request, sizeof(request),
        "POST /ask HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        job->host, job->port, body_len, body);

    int fd = tcp_connect(job->host, job->port);
    if (fd < 0) goto done;

    struct timeval tv = { ASK_HTTP_TIMEOUT_S, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (!write_all(fd, request, (size_t)req_len)) { close(fd); goto done; }

    char *response = read_all(fd);
    close(fd);
    if (response == NULL) goto done;

    char *body_ptr = http_body(response);
    if (body_ptr != NULL)
        answer_raw = json_extract_answer(body_ptr);
    free(response);

    if (answer_raw != NULL && answer_raw[0] != '\0')
    {
        /* Truncate before wrapping to cap the work. */
        if (strlen(answer_raw) > ASK_MAX_REPLY)
            answer_raw[ASK_MAX_REPLY] = '\0';
        answer_wrapped = wrap_text(answer_raw, ASK_WRAP_COLS);
    }

done:
    free(answer_raw);
    ask_enqueue_reply(job->ch, answer_wrapped);  /* answer_wrapped may be NULL = silent fail */
    free(job);
    return NULL;
}

/* -------- public API -------- */

void do_ask(CHAR_DATA *ch, char *argument)
{
    if (ch == NULL || ch->desc == NULL) return;

    /* Strip leading whitespace. */
    while (*argument && isspace((unsigned char)*argument)) argument++;

    if (*argument == '\0')
    {
        send_to_char("#YAsk what?#n Syntax: #cask <question>#n\n\r", ch);
        return;
    }
    if (strlen(argument) >= ASK_MAX_QUESTION)
    {
        send_to_char("#rThat question is too long.#n\n\r", ch);
        return;
    }
    if (ask_is_pending(ch))
    {
        send_to_char("#yThe Loremaster is still considering your previous question.#n\n\r", ch);
        return;
    }

    ASK_JOB *job = malloc(sizeof(*job));
    if (job == NULL)
    {
        send_to_char("#rThe Loremaster is silent.#n\n\r", ch);
        return;
    }
    job->ch = ch;
    strncpy(job->question, argument, sizeof(job->question) - 1);
    job->question[sizeof(job->question) - 1] = '\0';

    const char *host_env = getenv("RAG_HOST");
    const char *port_env = getenv("RAG_PORT");
    strncpy(job->host, host_env && *host_env ? host_env : "127.0.0.1", sizeof(job->host) - 1);
    job->host[sizeof(job->host) - 1] = '\0';
    job->port = (port_env && *port_env) ? atoi(port_env) : 8765;
    if (job->port <= 0) job->port = 8765;

    ask_mark_active(ch);

    pthread_t       th;
    pthread_attr_t  attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, ask_worker, job) != 0)
    {
        pthread_attr_destroy(&attr);
        ask_clear_active(ch);
        free(job);
        send_to_char("#rThe Loremaster is silent.#n\n\r", ch);
        return;
    }
    pthread_attr_destroy(&attr);

    send_to_char("#YYou ponder the question... #c(the Loremaster will reply shortly)#n\n\r", ch);
}

/*
 * Called once per game loop iteration. Delivers any ready answers, clears
 * the active flag, and emits periodic "thinking..." nudges for requests
 * still in flight.
 */
void ask_pump(void)
{
    ASK_PENDING *batch, *p, *next;

    pthread_mutex_lock(&ask_mutex);
    batch = ask_replies;
    ask_replies = NULL;
    pthread_mutex_unlock(&ask_mutex);

    for (p = batch; p != NULL; p = next)
    {
        next = p->next;
        if (ch_still_alive(p->ch))
        {
            if (p->reply != NULL)
            {
                send_to_char("\n\r#Y>> #CThe Loremaster speaks:#n\n\r", p->ch);
                send_to_char(p->reply, p->ch);
                send_to_char("#Y<<#n\n\r", p->ch);
            }
            /* reply == NULL: silent fail, per spec. */
        }
        ask_clear_active(p->ch);
        free(p->reply);
        free(p);
    }

    /*
     * Periodic "thinking..." nudges for in-flight requests. Snapshot the
     * (ch, tick-due?) pairs under the lock, update next_tick in place,
     * then send outside the lock -- send_to_char can call back into
     * other systems and we don't want to hold ask_mutex across that.
     */
    {
        CHAR_DATA  *due_ch[16];
        int         due_count = 0;
        ASK_PENDING *ap;

        pthread_mutex_lock(&ask_mutex);
        for (ap = ask_active; ap != NULL; ap = ap->next)
        {
            if (current_time >= ap->next_tick)
            {
                if (due_count < (int)(sizeof(due_ch) / sizeof(due_ch[0])))
                    due_ch[due_count++] = ap->ch;
                ap->next_tick = current_time + ASK_THINK_INTERVAL;
            }
        }
        pthread_mutex_unlock(&ask_mutex);

        for (int i = 0; i < due_count; i++)
        {
            if (ch_still_alive(due_ch[i]))
                send_to_char("#c... the Loremaster is still thinking ...#n\n\r", due_ch[i]);
        }
    }
}

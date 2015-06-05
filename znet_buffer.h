/* znet_buffer - send/recv buffer for znet */
#ifndef znet_buffer_h
#define znet_buffer_h


#ifndef ZN_NS_BEGIN
# ifdef __cplusplus
#   define ZN_NS_BEGIN extern "C" {
#   define ZN_NS_END   }
# else
#   define ZN_NS_BEGIN
#   define ZN_NS_END
# endif
#endif /* ZN_NS_BEGIN */

#if !defined(ZN_API) && defined(_WIN32)
# ifdef ZN_IMPLEMENTATION
#  define ZN_API __declspec(dllexport)
# else
#  define ZN_API __declspec(dllimport)
# endif
#endif

#ifndef ZN_API
# define ZN_API extern
#endif

#define ZN_BUFFERSIZE 8192


#include <stddef.h>

ZN_NS_BEGIN


/* buffer */

typedef struct zn_Buffer {
    char *buff;
    size_t size, used;
    char init_buff[ZN_BUFFERSIZE];
} zn_Buffer;

#define zn_addsize(b, sz) ((b)->used += (sz))
#define zn_addchar(b, ch) \
    ((void)((b)->used < (b)->size || zn_prepbuffsize((b), 1)), \
     ((b)->buff[(b)->used++] = (ch)))

ZN_API void zn_initbuffer  (zn_Buffer *b);
ZN_API void zn_resetbuffer (zn_Buffer *b);

ZN_API size_t zn_resizebuffer (zn_Buffer *b, size_t len);

ZN_API char *zn_prepbuffsize (zn_Buffer *b, size_t len);
ZN_API void zn_addlstring    (zn_Buffer *b, const char *s, size_t len);


/* recv buffer */

typedef size_t zn_HeaderHandler (void *ud, const char *buff, size_t len);
typedef void   zn_PacketHandler (void *ud, const char *buff, size_t len);

typedef struct zn_RecvBuffer {
    zn_HeaderHandler *header_handler; void *header_ud;
    zn_PacketHandler *packet_handler; void *packet_ud;
    size_t expected;
    zn_Buffer readed;
    char buff[ZN_BUFFERSIZE];
} zn_RecvBuffer;

ZN_API void zn_initrecvbuffer (zn_RecvBuffer *b);
ZN_API void zn_recvonheader   (zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud);
ZN_API void zn_recvonpacket   (zn_RecvBuffer *b, zn_PacketHandler *h, void *ud);

ZN_API char *zn_recvprepare (zn_RecvBuffer *b, size_t *plen);
ZN_API int   zn_recvfinish  (zn_RecvBuffer *b, size_t count);


/* send buffer */

typedef struct zn_SendBuffer {
    zn_Buffer *current;
    size_t sent_count;
    zn_Buffer sending;
    zn_Buffer pending;
} zn_SendBuffer;

ZN_API void zn_initsend  (zn_SendBuffer *sb);
ZN_API void zn_resetsend (zn_SendBuffer *sb);

ZN_API char *zn_sendprepare (zn_SendBuffer *sb, size_t len);
ZN_API int   zn_sendfinish  (zn_SendBuffer *sb, size_t count);


ZN_NS_END

#endif /* znet_buffer_h */


#ifdef ZN_IMPLEMENTATION


#include <stdlib.h>
#include <string.h>

ZN_NS_BEGIN


/* buffer */

#define ZN_MAX_SIZET (~(size_t)0 - 100)

ZN_API void zn_initbuffer(zn_Buffer *b) {
    b->buff = b->init_buff;
    b->size = ZN_BUFFERSIZE;
    b->used = 0;
}

ZN_API void zn_resetbuffer(zn_Buffer *b) {
    if (b->buff != b->init_buff)
        free(b->buff);
    zn_initbuffer(b);
}

ZN_API size_t zn_resizebuffer(zn_Buffer *b, size_t len) {
    char *newbuff;
    size_t newsize = b->size;
    while (newsize < ZN_MAX_SIZET/2 && newsize < len)
        newsize <<= 1;
    newbuff = b->buff != b->init_buff ? b->buff : NULL;
    if (newsize >= len
            && ((newbuff = (char*)realloc(newbuff, newsize)) != NULL))
    {
        b->buff = newbuff;
        b->size = newsize;
    }
    return b->size;
}

ZN_API char *zn_prepbuffsize(zn_Buffer *b, size_t len) {
    if (b->used + len > b->size && zn_resizebuffer(b, b->size+len) == b->size)
        return NULL;
    return &b->buff[b->used];
}

ZN_API void zn_addlstring(zn_Buffer *b, const char *s, size_t len) {
    memcpy(zn_prepbuffsize(b, len), s, len);
    b->used += len;
}

/* recv buffer */

static void   zn_def_onpacket (void *ud, const char *buff, size_t len) {}
static size_t zn_def_onheader (void *ud, const char *buff, size_t len)
{ return len; }

ZN_API void zn_initrecvbuffer (zn_RecvBuffer *b) {
    b->header_handler = zn_def_onheader; b->header_ud = NULL;
    b->packet_handler = zn_def_onpacket; b->packet_handler = NULL;
    b->expected = 0;
    zn_initbuffer(&b->readed);
}

ZN_API void zn_recvonheader(zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud)
{ b->header_handler = h; b->header_ud = ud; }

ZN_API void zn_recvonpacket(zn_RecvBuffer *b, zn_PacketHandler *h, void *ud)
{ b->packet_handler = h; b->packet_ud = ud; }

ZN_API char *zn_recvprepare(zn_RecvBuffer *b, size_t *plen) {
    *plen = ZN_BUFFERSIZE;
    return b->buff;
}

ZN_API int zn_recvfinish(zn_RecvBuffer *b, size_t count) {
    char *buff = b->buff;
again:
    if (count == 0) return 0;

    if (b->expected == 0) {
        size_t ret = b->header_handler(b->header_ud, buff, count);
        if (ret == 0) { /* need more result */
            zn_addlstring(&b->readed, buff, count);
            return 1;
        }
        if (ret <= count) {
            b->packet_handler(b->packet_ud, buff, ret);
            buff += ret;
            count -= ret;
            goto again;
        }
        b->expected = ret;
        zn_addlstring(&b->readed, buff, count);
        return 1;
    }

    if (b->readed.used + count >= b->expected) {
        size_t remaining = b->expected - (b->readed.used + count);
        zn_addlstring(&b->readed, buff, remaining);
        b->packet_handler(b->packet_ud, b->readed.buff, b->expected);
        buff += remaining;
        count -= remaining;
        b->expected = 0;
        b->readed.used = 0;
        goto again;
    }

    zn_addlstring(&b->readed, buff, count);
    return 1;
}


/* send buffer */

ZN_API void zn_initsend(zn_SendBuffer *b) {
    b->current = &b->sending;
    b->sent_count = 0;
    zn_initbuffer(&b->sending);
    zn_initbuffer(&b->pending);
}

ZN_API void zn_resetsend (zn_SendBuffer *b) {
    b->current = &b->sending;
    b->sent_count = 0;
    zn_resetbuffer(&b->sending);
    zn_resetbuffer(&b->pending);
}

ZN_API char *zn_sendprepare(zn_SendBuffer *b, size_t len) {
    char *buff;
    zn_Buffer *pending = b->current == &b->sending ?
        &b->pending : &b->sending;
    if (b->current->used == 0)
        pending = b->current;
    buff = zn_prepbuffsize(pending, len);
    zn_addsize(pending, len);
    return buff;
}

ZN_API int zn_sendfinish(zn_SendBuffer *b, size_t count) {
    zn_Buffer *pending = b->current == &b->sending ?
        &b->pending : &b->sending;
    if (b->current->used == count) { /* all sent? */
        zn_Buffer *tmp;
        b->current->used = 0;
        b->sent_count = 0;
        tmp = pending;
        pending = b->current;
        b->current = tmp;
    }
    else { /* still has something to send? */
        char *buff = b->current->buff;
        b->sent_count += count;
        if (b->sent_count > b->current->used / 2) { /* too many garbage */
            size_t remaining = b->current->used - b->sent_count;
            memmove(buff, buff + b->sent_count, remaining);
            b->current->used = remaining;
            b->sent_count = 0;
        }
        if (pending->used != 0) {
            zn_addlstring(b->current, pending->buff, pending->used);
            pending->used = 0;
        }
    }
    return b->current->used != 0;
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* cc: flags+='-mdll -s -O3 -DZN_IMPLEMENTATION -xc' output='znet_buffer.dll' */

/*
 * Copyright (c) 2015-2017, Ieshen Zheng <ieshen.zheng at 163 dot com>
 * Copyright (c) 2020, Nick <heronr1 at gmail dot com>
 * Copyright (c) 2020-2021, Bjorn Svensson <bjorn.a.svensson at est dot tech>
 * Copyright (c) 2021, Red Hat
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __HIRCLUSTER_H
#define __HIRCLUSTER_H

#include "dict.h"
#include <hiredis/async.h>
#include <hiredis/hiredis.h>

#define UNUSED(x) (void)(x)

#define HIREDIS_CLUSTER_MAJOR 0
#define HIREDIS_CLUSTER_MINOR 13
#define HIREDIS_CLUSTER_PATCH 0
#define HIREDIS_CLUSTER_SONAME 0.13

#define REDIS_CLUSTER_SLOTS 16384

#define REDIS_ROLE_NULL 0
#define REDIS_ROLE_MASTER 1
#define REDIS_ROLE_SLAVE 2

/* Configuration flags */
#define HIRCLUSTER_FLAG_NULL 0x0
/* Flag to enable parsing of slave nodes. Currently not used, but the
   information is added to its master node structure. */
#define HIRCLUSTER_FLAG_ADD_SLAVE 0x1000
/* Flag to enable parsing of importing/migrating slots for master nodes.
 * Only applicable when 'cluster nodes' command is used for route updates. */
#define HIRCLUSTER_FLAG_ADD_OPENSLOT 0x2000
/* Flag to enable routing table updates using the command 'cluster slots'.
 * Default is the 'cluster nodes' command. */
#define HIRCLUSTER_FLAG_ROUTE_USE_SLOTS 0x4000

/* Events, for redisClusterSetEventCallback() */
#define HIRCLUSTER_EVENT_SLOTMAP_UPDATED 1
#define HIRCLUSTER_EVENT_READY 2
#define HIRCLUSTER_EVENT_FREE_CONTEXT 3

#ifdef __cplusplus
extern "C" {
#endif

struct dict;
struct hilist;
struct redisClusterAsyncContext;

typedef int(adapterAttachFn)(redisAsyncContext *, void *);
typedef int(sslInitFn)(redisContext *, void *);
typedef void(redisClusterCallbackFn)(struct redisClusterAsyncContext *, void *,
                                     void *);
typedef struct redisClusterNode {
    sds name;
    sds addr;
    sds host;
    uint16_t port;
    uint8_t role;
    uint8_t pad;
    int failure_count; /* consecutive failing attempts in async */
    redisContext *con;
    redisAsyncContext *acon;
    int64_t lastConnectionAttempt; /* Timestamp */
    struct hilist *slots;
    struct hilist *slaves;
    struct hiarray *migrating; /* copen_slot[] */
    struct hiarray *importing; /* copen_slot[] */
} redisClusterNode;

typedef struct cluster_slot {
    uint32_t start;
    uint32_t end;
    redisClusterNode *node; /* master that this slot region belong to */
} cluster_slot;

typedef struct copen_slot {
    uint32_t slot_num; /* slot number */
    int migrate;       /* migrating or importing? */
    sds remote_name;   /* name of node this slot migrating to/importing from */
    redisClusterNode *node; /* master that this slot belong to */
} copen_slot;

/* Context for accessing a Redis Cluster */
typedef struct redisClusterContext {
    int err;          /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */

    /* Configurations */
    int flags;                       /* Configuration flags */
    struct timeval *connect_timeout; /* TCP connect timeout */
    struct timeval *command_timeout; /* Receive and send timeout */
    int max_retry_count;             /* Allowed retry attempts */
    char *username;                  /* Authenticate using user */
    char *password;                  /* Authentication password */

    struct dict *nodes;       /* Known redisClusterNode's */
    uint64_t route_version;   /* Increased when the node lookup table changes */
    redisClusterNode **table; /* redisClusterNode lookup table */

    struct hilist *requests; /* Outstanding commands (Pipelining) */

    int retry_count;       /* Current number of failing attempts */
    int need_update_route; /* Indicator for redisClusterReset() (Pipel.) */

    void *ssl; /* Pointer to a redisSSLContext when using SSL/TLS. */
    sslInitFn *ssl_init_fn; /* Func ptr for SSL context initiation */

    void (*on_connect)(const struct redisContext *c, int status);
    void (*event_callback)(const struct redisClusterContext *cc, int event,
                           void *privdata);
    void *event_privdata;

} redisClusterContext;

/* Context for accessing a Redis Cluster asynchronously */
typedef struct redisClusterAsyncContext {
    redisClusterContext *cc;

    int err;          /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */

    int64_t lastSlotmapUpdateAttempt; /* Timestamp */

    void *adapter;              /* Adapter to the async event library */
    adapterAttachFn *attach_fn; /* Func ptr for attaching the async library */

    /* Called when either the connection is terminated due to an error or per
     * user request. The status is set accordingly (REDIS_OK, REDIS_ERR). */
    redisDisconnectCallback *onDisconnect;

    /* Called when the first write event was received. */
    redisConnectCallback *onConnect;

} redisClusterAsyncContext;

typedef struct redisClusterNodeIterator {
    redisClusterContext *cc;
    uint64_t route_version;
    int retries_left;
    dictIterator di;
} redisClusterNodeIterator;

/*
 * Synchronous API
 */

redisClusterContext *redisClusterConnect(const char *addrs, int flags);
redisClusterContext *redisClusterConnectWithTimeout(const char *addrs,
                                                    const struct timeval tv,
                                                    int flags);
int redisClusterConnect2(redisClusterContext *cc);

redisClusterContext *redisClusterContextInit(void);
void redisClusterFree(redisClusterContext *cc);

/* Configuration options */
int redisClusterSetOptionAddNode(redisClusterContext *cc, const char *addr);
int redisClusterSetOptionAddNodes(redisClusterContext *cc, const char *addrs);
/* Deprecated function, option has no effect. */
int redisClusterSetOptionConnectBlock(redisClusterContext *cc);
/* Deprecated function, option has no effect. */
int redisClusterSetOptionConnectNonBlock(redisClusterContext *cc);
int redisClusterSetOptionUsername(redisClusterContext *cc,
                                  const char *username);
int redisClusterSetOptionPassword(redisClusterContext *cc,
                                  const char *password);
int redisClusterSetOptionParseSlaves(redisClusterContext *cc);
int redisClusterSetOptionParseOpenSlots(redisClusterContext *cc);
int redisClusterSetOptionRouteUseSlots(redisClusterContext *cc);
int redisClusterSetOptionConnectTimeout(redisClusterContext *cc,
                                        const struct timeval tv);
int redisClusterSetOptionTimeout(redisClusterContext *cc,
                                 const struct timeval tv);
int redisClusterSetOptionMaxRetry(redisClusterContext *cc, int max_retry_count);
/* Deprecated function, replaced with redisClusterSetOptionMaxRetry() */
void redisClusterSetMaxRedirect(redisClusterContext *cc,
                                int max_redirect_count);
/* A hook for connect and reconnect attempts, e.g. for applying additional
 * socket options. This is called just after connect, before TLS handshake and
 * Redis authentication.
 *
 * On successful connection, `status` is set to `REDIS_OK` and the file
 * descriptor can be accessed as `c->fd` to apply socket options.
 *
 * On failed connection attempt, this callback is called with `status` set to
 * `REDIS_ERR`. The `err` field in the `redisContext` can be used to find out
 * the cause of the error. */
int redisClusterSetConnectCallback(redisClusterContext *cc,
                                   void(fn)(const redisContext *c, int status));

/* A hook for events. */
int redisClusterSetEventCallback(redisClusterContext *cc,
                                 void(fn)(const redisClusterContext *cc,
                                          int event, void *privdata),
                                 void *privdata);

/* Blocking
 * The following functions will block for a reply, or return NULL if there was
 * an error in performing the command.
 */

/* Variadic commands (like printf) */
void *redisClusterCommand(redisClusterContext *cc, const char *format, ...);
void *redisClusterCommandToNode(redisClusterContext *cc, redisClusterNode *node,
                                const char *format, ...);
/* Variadic using va_list */
void *redisClustervCommand(redisClusterContext *cc, const char *format,
                           va_list ap);
/* Using argc and argv */
void *redisClusterCommandArgv(redisClusterContext *cc, int argc,
                              const char **argv, const size_t *argvlen);
/* Send a Redis protocol encoded string */
void *redisClusterFormattedCommand(redisClusterContext *cc, char *cmd, int len);

/* Pipelining
 * The following functions will write a command to the output buffer.
 * A call to `redisClusterGetReply()` will flush all commands in the output
 * buffer and read until it has a reply from the first command in the buffer.
 */

/* Variadic commands (like printf) */
int redisClusterAppendCommand(redisClusterContext *cc, const char *format, ...);
int redisClusterAppendCommandToNode(redisClusterContext *cc,
                                    redisClusterNode *node, const char *format,
                                    ...);
/* Variadic using va_list */
int redisClustervAppendCommand(redisClusterContext *cc, const char *format,
                               va_list ap);
/* Using argc and argv */
int redisClusterAppendCommandArgv(redisClusterContext *cc, int argc,
                                  const char **argv, const size_t *argvlen);
/* Use a Redis protocol encoded string as command */
int redisClusterAppendFormattedCommand(redisClusterContext *cc, char *cmd,
                                       int len);
/* Flush output buffer and return first reply */
int redisClusterGetReply(redisClusterContext *cc, void **reply);

/* Reset context after a performed pipelining */
void redisClusterReset(redisClusterContext *cc);

/* Update the slotmap by querying any node. */
int redisClusterUpdateSlotmap(redisClusterContext *cc);

/* Internal functions */
redisContext *ctx_get_by_node(redisClusterContext *cc, redisClusterNode *node);
struct dict *parse_cluster_nodes(redisClusterContext *cc, char *str,
                                 int str_len, int flags);
struct dict *parse_cluster_slots(redisClusterContext *cc, redisReply *reply,
                                 int flags);

/*
 * Asynchronous API
 */

redisClusterAsyncContext *redisClusterAsyncContextInit(void);
void redisClusterAsyncFree(redisClusterAsyncContext *acc);

int redisClusterAsyncSetConnectCallback(redisClusterAsyncContext *acc,
                                        redisConnectCallback *fn);
int redisClusterAsyncSetDisconnectCallback(redisClusterAsyncContext *acc,
                                           redisDisconnectCallback *fn);

/* Connect and update slotmap, will block until complete. */
redisClusterAsyncContext *redisClusterAsyncConnect(const char *addrs,
                                                   int flags);
/* Connect and update slotmap asynchronously using configured event engine. */
int redisClusterAsyncConnect2(redisClusterAsyncContext *acc);
void redisClusterAsyncDisconnect(redisClusterAsyncContext *acc);

/* Commands */
int redisClusterAsyncCommand(redisClusterAsyncContext *acc,
                             redisClusterCallbackFn *fn, void *privdata,
                             const char *format, ...);
int redisClusterAsyncCommandToNode(redisClusterAsyncContext *acc,
                                   redisClusterNode *node,
                                   redisClusterCallbackFn *fn, void *privdata,
                                   const char *format, ...);
int redisClustervAsyncCommand(redisClusterAsyncContext *acc,
                              redisClusterCallbackFn *fn, void *privdata,
                              const char *format, va_list ap);
int redisClusterAsyncCommandArgv(redisClusterAsyncContext *acc,
                                 redisClusterCallbackFn *fn, void *privdata,
                                 int argc, const char **argv,
                                 const size_t *argvlen);
int redisClusterAsyncCommandArgvToNode(redisClusterAsyncContext *acc,
                                       redisClusterNode *node,
                                       redisClusterCallbackFn *fn,
                                       void *privdata, int argc,
                                       const char **argv,
                                       const size_t *argvlen);

/* Use a Redis protocol encoded string as command */
int redisClusterAsyncFormattedCommand(redisClusterAsyncContext *acc,
                                      redisClusterCallbackFn *fn,
                                      void *privdata, char *cmd, int len);
int redisClusterAsyncFormattedCommandToNode(redisClusterAsyncContext *acc,
                                            redisClusterNode *node,
                                            redisClusterCallbackFn *fn,
                                            void *privdata, char *cmd, int len);

/* Internal functions */
redisAsyncContext *actx_get_by_node(redisClusterAsyncContext *acc,
                                    redisClusterNode *node);

/* Cluster node iterator functions */
void redisClusterInitNodeIterator(redisClusterNodeIterator *iter,
                                  redisClusterContext *cc);
redisClusterNode *redisClusterNodeNext(redisClusterNodeIterator *iter);

/* Helper functions */
unsigned int redisClusterGetSlotByKey(char *key);
redisClusterNode *redisClusterGetNodeByKey(redisClusterContext *cc, char *key);

/* Old names of renamed functions and types, kept for backward compatibility. */
#ifndef HIRCLUSTER_NO_OLD_NAMES
#define cluster_update_route redisClusterUpdateSlotmap
#define initNodeIterator redisClusterInitNodeIterator
#define nodeNext redisClusterNodeNext
#define redisClusterConnectNonBlock redisClusterConnect
typedef struct redisClusterNode cluster_node;
typedef struct redisClusterNodeIterator nodeIterator;
#endif

#ifdef __cplusplus
}
#endif

#endif

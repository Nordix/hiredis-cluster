#ifndef __HIRCLUSTER_H
#define __HIRCLUSTER_H

#include <hiredis/async.h>
#include <hiredis/hiredis.h>

#ifdef SSL_SUPPORT
#include <hiredis/hiredis_ssl.h>
#endif

#define UNUSED(x) (void)(x)

#define HIREDIS_CLUSTER_MAJOR 0
#define HIREDIS_CLUSTER_MINOR 5
#define HIREDIS_CLUSTER_PATCH 0
#define HIREDIS_CLUSTER_SONAME 0.5

#define REDIS_CLUSTER_SLOTS 16384

#define REDIS_ROLE_NULL 0
#define REDIS_ROLE_MASTER 1
#define REDIS_ROLE_SLAVE 2

#define CONFIG_AUTHPASS_MAX_LEN 512 // Defined in Redis as max characters

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

#ifdef __cplusplus
extern "C" {
#endif

struct dict;
struct hilist;
struct redisClusterAsyncContext;

typedef int(adapterAttachFn)(redisAsyncContext *, void *);
typedef void(redisClusterCallbackFn)(struct redisClusterAsyncContext *, void *,
                                     void *);
typedef struct cluster_node {
    sds name;
    sds addr;
    sds host;
    int port;
    uint8_t role;
    redisContext *con;
    redisAsyncContext *acon;
    struct hilist *slots;
    struct hilist *slaves;
    int failure_count;         /* consecutive failing attempts in async */
    struct hiarray *migrating; /* copen_slot[] */
    struct hiarray *importing; /* copen_slot[] */
} cluster_node;

typedef struct cluster_slot {
    uint32_t start;
    uint32_t end;
    cluster_node *node; /* master that this slot region belong to */
} cluster_slot;

typedef struct copen_slot {
    uint32_t slot_num;  /* slot number */
    int migrate;        /* migrating or importing? */
    sds remote_name;    /* name of node this slot migrating to/importing from */
    cluster_node *node; /* master that this slot belong to */
} copen_slot;

/* Context for accessing a Redis Cluster */
typedef struct redisClusterContext {
    int err;          /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */

    /* Configurations */
    int flags;                                  /* Configuration flags */
    struct timeval *connect_timeout;            /* TCP connect timeout */
    struct timeval *command_timeout;            /* Receive and send timeout */
    int max_redirect_count;                     /* Allowed retry attempts */
    char password[CONFIG_AUTHPASS_MAX_LEN + 1]; /* Include a null terminator */

    struct dict *nodes;     /* Known cluster_nodes*/
    struct hiarray *slots;  /* Sorted array of cluster_slots */
    uint64_t route_version; /* Increased when the node lookup table changes */
    cluster_node *table[REDIS_CLUSTER_SLOTS]; /* cluster_node lookup table */

    struct hilist *requests; /* Outstanding commands (Pipelining) */

    int retry_count;           /* Current number of failing attempts */
    int need_update_route;     /* Indicator for redisClusterReset() (Pipel.) */
    int64_t update_route_time; /* Timestamp for next required route update
                                  (Async mode only) */
#ifdef SSL_SUPPORT
    redisSSLContext *ssl;
#endif

} redisClusterContext;

/* Context for accessing a Redis Cluster asynchronously */
typedef struct redisClusterAsyncContext {
    redisClusterContext *cc;

    int err;          /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */

    void *adapter;              /* Adapter to the async event library */
    adapterAttachFn *attach_fn; /* Func ptr for attaching the async library */

    /* Called when either the connection is terminated due to an error or per
     * user request. The status is set accordingly (REDIS_OK, REDIS_ERR). */
    redisDisconnectCallback *onDisconnect;

    /* Called when the first write event was received. */
    redisConnectCallback *onConnect;

} redisClusterAsyncContext;

/*
 * Synchronous API
 */

redisClusterContext *redisClusterConnect(const char *addrs, int flags);
redisClusterContext *redisClusterConnectWithTimeout(const char *addrs,
                                                    const struct timeval tv,
                                                    int flags);
redisClusterContext *redisClusterConnectNonBlock(const char *addrs, int flags);
int redisClusterConnect2(redisClusterContext *cc);

redisClusterContext *redisClusterContextInit(void);
void redisClusterFree(redisClusterContext *cc);

/* Configuration options */
int redisClusterSetOptionAddNode(redisClusterContext *cc, const char *addr);
int redisClusterSetOptionAddNodes(redisClusterContext *cc, const char *addrs);
int redisClusterSetOptionConnectBlock(redisClusterContext *cc);
int redisClusterSetOptionConnectNonBlock(redisClusterContext *cc);
int redisClusterSetOptionPassword(redisClusterContext *cc,
                                  const char *password);
int redisClusterSetOptionParseSlaves(redisClusterContext *cc);
int redisClusterSetOptionParseOpenSlots(redisClusterContext *cc);
int redisClusterSetOptionRouteUseSlots(redisClusterContext *cc);
int redisClusterSetOptionConnectTimeout(redisClusterContext *cc,
                                        const struct timeval tv);
int redisClusterSetOptionTimeout(redisClusterContext *cc,
                                 const struct timeval tv);
int redisClusterSetOptionMaxRedirect(redisClusterContext *cc,
                                     int max_redirect_count);
#ifdef SSL_SUPPORT
int redisClusterSetOptionEnableSSL(redisClusterContext *cc,
                                   redisSSLContext *ssl);
#endif
/* Deprecated function, replaced with redisClusterSetOptionMaxRedirect() */
void redisClusterSetMaxRedirect(redisClusterContext *cc,
                                int max_redirect_count);

/* Blocking
 * The following functions will block for a reply, or return NULL if there was
 * an error in performing the command.
 */

/* Variadic commands (like printf) */
void *redisClusterCommand(redisClusterContext *cc, const char *format, ...);
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

/* Internal functions */
int cluster_update_route(redisClusterContext *cc);
redisContext *ctx_get_by_node(redisClusterContext *cc,
                              struct cluster_node *node);
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

redisClusterAsyncContext *redisClusterAsyncConnect(const char *addrs,
                                                   int flags);
void redisClusterAsyncDisconnect(redisClusterAsyncContext *acc);

/* Commands */
int redisClusterAsyncCommand(redisClusterAsyncContext *acc,
                             redisClusterCallbackFn *fn, void *privdata,
                             const char *format, ...);
int redisClustervAsyncCommand(redisClusterAsyncContext *acc,
                              redisClusterCallbackFn *fn, void *privdata,
                              const char *format, va_list ap);
int redisClusterAsyncCommandArgv(redisClusterAsyncContext *acc,
                                 redisClusterCallbackFn *fn, void *privdata,
                                 int argc, const char **argv,
                                 const size_t *argvlen);

/* Use a Redis protocol encoded string as command */
int redisClusterAsyncFormattedCommand(redisClusterAsyncContext *acc,
                                      redisClusterCallbackFn *fn,
                                      void *privdata, char *cmd, int len);

/* Internal functions */
redisAsyncContext *actx_get_by_node(redisClusterAsyncContext *acc,
                                    cluster_node *node);
#ifdef __cplusplus
}
#endif

#endif

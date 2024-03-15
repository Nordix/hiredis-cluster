/*
 * Copyright (c) 2015-2017, Ieshen Zheng <ieshen.zheng at 163 dot com>
 * Copyright (c) 2020, Nick <heronr1 at gmail dot com>
 * Copyright (c) 2020-2021, Bjorn Svensson <bjorn.a.svensson at est dot tech>
 * Copyright (c) 2020-2021, Viktor Söderqvist <viktor.soderqvist at est dot tech>
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
#define _XOPEN_SOURCE 600
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <hiredis/alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adlist.h"
#include "command.h"
#include "dict.h"
#include "hiarray.h"
#include "hircluster.h"
#include "hiutil.h"
#include "win32.h"

// Cluster errors are offset by 100 to be sufficiently out of range of
// standard Redis errors
#define REDIS_ERR_CLUSTER_TOO_MANY_RETRIES 100

#define REDIS_ERROR_MOVED "MOVED"
#define REDIS_ERROR_ASK "ASK"
#define REDIS_ERROR_TRYAGAIN "TRYAGAIN"
#define REDIS_ERROR_CLUSTERDOWN "CLUSTERDOWN"

#define REDIS_STATUS_OK "OK"

#define REDIS_COMMAND_CLUSTER_NODES "CLUSTER NODES"
#define REDIS_COMMAND_CLUSTER_SLOTS "CLUSTER SLOTS"
#define REDIS_COMMAND_ASKING "ASKING"

#define IP_PORT_SEPARATOR ':'

#define PORT_CPORT_SEPARATOR '@'

#define CLUSTER_ADDRESS_SEPARATOR ","

#define CLUSTER_DEFAULT_MAX_RETRY_COUNT 5
#define NO_RETRY -1

#define CRLF "\x0d\x0a"
#define CRLF_LEN (sizeof("\x0d\x0a") - 1)

#define SLOTMAP_UPDATE_THROTTLE_USEC 1000000
#define SLOTMAP_UPDATE_ONGOING INT64_MAX

typedef struct cluster_async_data {
    redisClusterAsyncContext *acc;
    struct cmd *command;
    redisClusterCallbackFn *callback;
    int retry_count;
    void *privdata;
} cluster_async_data;

typedef enum CLUSTER_ERR_TYPE {
    CLUSTER_NOT_ERR = 0,
    CLUSTER_ERR_MOVED,
    CLUSTER_ERR_ASK,
    CLUSTER_ERR_TRYAGAIN,
    CLUSTER_ERR_CLUSTERDOWN,
    CLUSTER_ERR_SENTINEL
} CLUSTER_ERR_TYPE;

static void freeRedisClusterNode(redisClusterNode *node);
static void cluster_slot_destroy(cluster_slot *slot);
static void cluster_open_slot_destroy(copen_slot *oslot);
static int updateNodesAndSlotmap(redisClusterContext *cc, dict *nodes);
static int updateSlotMapAsync(redisClusterAsyncContext *acc,
                              redisAsyncContext *ac);

void listClusterNodeDestructor(void *val) { freeRedisClusterNode(val); }

void listClusterSlotDestructor(void *val) { cluster_slot_destroy(val); }

unsigned int dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char *)key, sdslen((char *)key));
}

int dictSdsKeyCompare(void *privdata, const void *key1, const void *key2) {
    int l1, l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2)
        return 0;
    return memcmp(key1, key2, l1) == 0;
}

void dictSdsDestructor(void *privdata, void *val) {
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

void dictClusterNodeDestructor(void *privdata, void *val) {
    DICT_NOTUSED(privdata);
    freeRedisClusterNode(val);
}

/* Cluster node hash table
 * maps node address (1.2.3.4:6379) to a redisClusterNode
 * Has ownership of redisClusterNode memory
 */
dictType clusterNodesDictType = {
    dictSdsHash,              /* hash function */
    NULL,                     /* key dup */
    NULL,                     /* val dup */
    dictSdsKeyCompare,        /* key compare */
    dictSdsDestructor,        /* key destructor */
    dictClusterNodeDestructor /* val destructor */
};

/* Referenced cluster node hash table
 * maps node id (437c719f5.....) to a redisClusterNode
 * No ownership of redisClusterNode memory
 */
dictType clusterNodesRefDictType = {
    dictSdsHash,       /* hash function */
    NULL,              /* key dup */
    NULL,              /* val dup */
    dictSdsKeyCompare, /* key compare */
    dictSdsDestructor, /* key destructor */
    NULL               /* val destructor */
};

void listCommandFree(void *command) {
    struct cmd *cmd = command;
    command_destroy(cmd);
}

/* -----------------------------------------------------------------------------
 * Key space handling
 * -------------------------------------------------------------------------- */

/* We have 16384 hash slots. The hash slot of a given key is obtained
 * as the least significant 14 bits of the crc16 of the key.
 *
 * However if the key contains the {...} pattern, only the part between
 * { and } is hashed. This may be useful in the future to force certain
 * keys to be in the same node (assuming no resharding is in progress). */
static unsigned int keyHashSlot(char *key, int keylen) {
    int s, e; /* start-end indexes of { and } */

    for (s = 0; s < keylen; s++)
        if (key[s] == '{')
            break;

    /* No '{' ? Hash the whole key. This is the base case. */
    if (s == keylen)
        return crc16(key, keylen) & 0x3FFF;

    /* '{' found? Check if we have the corresponding '}'. */
    for (e = s + 1; e < keylen; e++)
        if (key[e] == '}')
            break;

    /* No '}' or nothing betweeen {} ? Hash the whole key. */
    if (e == keylen || e == s + 1)
        return crc16(key, keylen) & 0x3FFF;

    /* If we are here there is both a { and a } on its right. Hash
     * what is in the middle between { and }. */
    return crc16(key + s + 1, e - s - 1) & 0x3FFF;
}

static void __redisClusterSetError(redisClusterContext *cc, int type,
                                   const char *str) {
    size_t len;

    if (cc == NULL) {
        return;
    }

    cc->err = type;
    if (str != NULL) {
        len = strlen(str);
        len = len < (sizeof(cc->errstr) - 1) ? len : (sizeof(cc->errstr) - 1);
        memcpy(cc->errstr, str, len);
        cc->errstr[len] = '\0';
    } else {
        /* Only REDIS_ERR_IO may lack a description! */
        assert(type == REDIS_ERR_IO);
        strerror_r(errno, cc->errstr, sizeof(cc->errstr));
    }
}

static int cluster_reply_error_type(redisReply *reply) {

    if (reply == NULL) {
        return REDIS_ERR;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        if ((int)strlen(REDIS_ERROR_MOVED) < reply->len &&
            memcmp(reply->str, REDIS_ERROR_MOVED, strlen(REDIS_ERROR_MOVED)) ==
                0) {
            return CLUSTER_ERR_MOVED;
        } else if ((int)strlen(REDIS_ERROR_ASK) < reply->len &&
                   memcmp(reply->str, REDIS_ERROR_ASK,
                          strlen(REDIS_ERROR_ASK)) == 0) {
            return CLUSTER_ERR_ASK;
        } else if ((int)strlen(REDIS_ERROR_TRYAGAIN) < reply->len &&
                   memcmp(reply->str, REDIS_ERROR_TRYAGAIN,
                          strlen(REDIS_ERROR_TRYAGAIN)) == 0) {
            return CLUSTER_ERR_TRYAGAIN;
        } else if ((int)strlen(REDIS_ERROR_CLUSTERDOWN) < reply->len &&
                   memcmp(reply->str, REDIS_ERROR_CLUSTERDOWN,
                          strlen(REDIS_ERROR_CLUSTERDOWN)) == 0) {
            return CLUSTER_ERR_CLUSTERDOWN;
        } else {
            return CLUSTER_ERR_SENTINEL;
        }
    }

    return CLUSTER_NOT_ERR;
}

/* Create and initiate the cluster node structure */
static redisClusterNode *createRedisClusterNode(void) {
    /* use calloc to guarantee all fields are zeroed */
    return hi_calloc(1, sizeof(redisClusterNode));
}

/* Cleanup the cluster node structure */
static void freeRedisClusterNode(redisClusterNode *node) {
    if (node == NULL) {
        return;
    }

    sdsfree(node->name);
    sdsfree(node->addr);
    sdsfree(node->host);
    redisFree(node->con);

    if (node->acon != NULL) {
        /* Detach this cluster node from the async context. This makes sure
         * that redisAsyncFree() wont attempt to update the pointer via its
         * dataCleanup and unlinkAsyncContextAndNode() */
        node->acon->data = NULL;
        redisAsyncFree(node->acon);
    }
    if (node->slots != NULL) {
        listRelease(node->slots);
    }
    if (node->slaves != NULL) {
        listRelease(node->slaves);
    }

    copen_slot **oslot;
    if (node->migrating) {
        while (hiarray_n(node->migrating)) {
            oslot = hiarray_pop(node->migrating);
            cluster_open_slot_destroy(*oslot);
        }
        hiarray_destroy(node->migrating);
    }
    if (node->importing) {
        while (hiarray_n(node->importing)) {
            oslot = hiarray_pop(node->importing);
            cluster_open_slot_destroy(*oslot);
        }
        hiarray_destroy(node->importing);
    }
    hi_free(node);
}

static cluster_slot *cluster_slot_create(redisClusterNode *node) {
    cluster_slot *slot;

    slot = hi_calloc(1, sizeof(*slot));
    if (slot == NULL) {
        return NULL;
    }
    slot->node = node;

    if (node != NULL) {
        ASSERT(node->role == REDIS_ROLE_MASTER);
        if (node->slots == NULL) {
            node->slots = listCreate();
            if (node->slots == NULL) {
                cluster_slot_destroy(slot);
                return NULL;
            }

            node->slots->free = listClusterSlotDestructor;
        }

        if (listAddNodeTail(node->slots, slot) == NULL) {
            cluster_slot_destroy(slot);
            return NULL;
        }
    }

    return slot;
}

static int cluster_slot_ref_node(cluster_slot *slot, redisClusterNode *node) {
    if (slot == NULL || node == NULL) {
        return REDIS_ERR;
    }

    if (node->role != REDIS_ROLE_MASTER) {
        return REDIS_ERR;
    }

    if (node->slots == NULL) {
        node->slots = listCreate();
        if (node->slots == NULL) {
            return REDIS_ERR;
        }

        node->slots->free = listClusterSlotDestructor;
    }

    if (listAddNodeTail(node->slots, slot) == NULL) {
        return REDIS_ERR;
    }
    slot->node = node;

    return REDIS_OK;
}

static void cluster_slot_destroy(cluster_slot *slot) {
    slot->start = 0;
    slot->end = 0;
    slot->node = NULL;

    hi_free(slot);
}

static copen_slot *cluster_open_slot_create(uint32_t slot_num, int migrate,
                                            sds remote_name,
                                            redisClusterNode *node) {
    copen_slot *oslot;

    oslot = hi_calloc(1, sizeof(*oslot));
    if (oslot == NULL) {
        return NULL;
    }

    oslot->slot_num = slot_num;
    oslot->migrate = migrate;
    oslot->node = node;
    oslot->remote_name = sdsdup(remote_name);
    if (oslot->remote_name == NULL) {
        hi_free(oslot);
        return NULL;
    }

    return oslot;
}

static void cluster_open_slot_destroy(copen_slot *oslot) {
    oslot->slot_num = 0;
    oslot->migrate = 0;
    oslot->node = NULL;
    sdsfree(oslot->remote_name);
    oslot->remote_name = NULL;
    hi_free(oslot);
}

/**
 * Handle password authentication in the synchronous API
 */
static int authenticate(redisClusterContext *cc, redisContext *c) {
    if (cc == NULL || c == NULL) {
        return REDIS_ERR;
    }

    // Skip if no password configured
    if (cc->password == NULL) {
        return REDIS_OK;
    }

    redisReply *reply;
    if (cc->username != NULL) {
        reply = redisCommand(c, "AUTH %s %s", cc->username, cc->password);
    } else {
        reply = redisCommand(c, "AUTH %s", cc->password);
    }

    if (reply == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "Command AUTH reply error (NULL)");
        goto error;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, reply->str);
        goto error;
    }

    freeReplyObject(reply);
    return REDIS_OK;

error:
    freeReplyObject(reply);

    return REDIS_ERR;
}

/**
 * Return a new node with the "cluster slots" command reply.
 */
static redisClusterNode *node_get_with_slots(redisClusterContext *cc,
                                             redisReply *host_elem,
                                             redisReply *port_elem,
                                             uint8_t role) {
    redisClusterNode *node = NULL;

    if (host_elem == NULL || port_elem == NULL) {
        return NULL;
    }

    if (host_elem->type != REDIS_REPLY_STRING || host_elem->len <= 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "Command(cluster slots) reply error: "
                               "node ip is not string.");
        goto error;
    }

    if (port_elem->type != REDIS_REPLY_INTEGER || port_elem->integer <= 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "Command(cluster slots) reply error: "
                               "node port is not integer.");
        goto error;
    }

    if (!hi_valid_port((int)port_elem->integer)) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "Command(cluster slots) reply error: "
                               "node port is not valid.");
        goto error;
    }

    node = createRedisClusterNode();
    if (node == NULL) {
        goto oom;
    }

    if (role == REDIS_ROLE_MASTER) {
        node->slots = listCreate();
        if (node->slots == NULL) {
            goto oom;
        }

        node->slots->free = listClusterSlotDestructor;
    }

    node->addr = sdsnewlen(host_elem->str, host_elem->len);
    if (node->addr == NULL) {
        goto oom;
    }
    node->addr = sdscatfmt(node->addr, ":%i", port_elem->integer);
    if (node->addr == NULL) {
        goto oom;
    }
    node->host = sdsnewlen(host_elem->str, host_elem->len);
    if (node->host == NULL) {
        goto oom;
    }
    node->name = NULL;
    node->port = (int)port_elem->integer;
    node->role = role;

    return node;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    if (node != NULL) {
        sdsfree(node->addr);
        sdsfree(node->host);
        hi_free(node);
    }
    return NULL;
}

/**
 * Return a new node with the "cluster nodes" command reply.
 */
static redisClusterNode *node_get_with_nodes(redisClusterContext *cc,
                                             sds *node_infos, int info_count,
                                             uint8_t role) {
    char *p = NULL;
    redisClusterNode *node = NULL;

    if (info_count < 8) {
        return NULL;
    }

    node = createRedisClusterNode();
    if (node == NULL) {
        goto oom;
    }

    if (role == REDIS_ROLE_MASTER) {
        node->slots = listCreate();
        if (node->slots == NULL) {
            goto oom;
        }

        node->slots->free = listClusterSlotDestructor;
    }

    /* Handle field <id> */
    node->name = node_infos[0];
    node_infos[0] = NULL; /* Ownership moved */

    /* Handle field <ip:port@cport...>
     * Remove @cport... since addr is used as a dict key which should be <ip>:<port> */
    if ((p = strchr(node_infos[1], PORT_CPORT_SEPARATOR)) != NULL) {
        sdsrange(node_infos[1], 0, p - node_infos[1] - 1 /* skip @ */);
    }
    node->addr = node_infos[1];
    node_infos[1] = NULL; /* Ownership moved */

    node->role = role;

    /* Get the ip part */
    if ((p = strrchr(node->addr, IP_PORT_SEPARATOR)) == NULL) {
        __redisClusterSetError(
            cc, REDIS_ERR_OTHER,
            "server address is incorrect, port separator missing.");
        goto error;
    }
    node->host = sdsnewlen(node->addr, p - node->addr);
    if (node->host == NULL) {
        goto oom;
    }
    p++; // remove found separator character

    /* Get the port part */
    node->port = hi_atoi(p, strlen(p));

    return node;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    freeRedisClusterNode(node);
    return NULL;
}

static void cluster_nodes_swap_ctx(dict *nodes_f, dict *nodes_t) {
    dictEntry *de_f, *de_t;
    redisClusterNode *node_f, *node_t;
    redisContext *c;
    redisAsyncContext *ac;

    if (nodes_f == NULL || nodes_t == NULL) {
        return;
    }

    dictIterator di;
    dictInitIterator(&di, nodes_t);

    while ((de_t = dictNext(&di)) != NULL) {
        node_t = dictGetEntryVal(de_t);
        if (node_t == NULL) {
            continue;
        }

        de_f = dictFind(nodes_f, node_t->addr);
        if (de_f == NULL) {
            continue;
        }

        node_f = dictGetEntryVal(de_f);
        if (node_f->con != NULL) {
            c = node_f->con;
            node_f->con = node_t->con;
            node_t->con = c;
        }

        if (node_f->acon != NULL) {
            ac = node_f->acon;
            node_f->acon = node_t->acon;
            node_t->acon = ac;

            node_t->acon->data = node_t;
            if (node_f->acon)
                node_f->acon->data = node_f;
        }
    }
}

static int cluster_master_slave_mapping_with_name(redisClusterContext *cc,
                                                  dict **nodes,
                                                  redisClusterNode *node,
                                                  sds master_name) {
    int ret;
    dictEntry *di;
    redisClusterNode *node_old;
    listNode *lnode;

    if (node == NULL || master_name == NULL) {
        return REDIS_ERR;
    }

    if (*nodes == NULL) {
        *nodes = dictCreate(&clusterNodesRefDictType, NULL);
        if (*nodes == NULL) {
            goto oom;
        }
    }

    di = dictFind(*nodes, master_name);
    if (di == NULL) {
        sds key = sdsnewlen(master_name, sdslen(master_name));
        if (key == NULL) {
            goto oom;
        }
        ret = dictAdd(*nodes, key, node);
        if (ret != DICT_OK) {
            sdsfree(key);
            goto oom;
        }

    } else {
        node_old = dictGetEntryVal(di);
        if (node_old == NULL) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER, "dict get value null");
            return REDIS_ERR;
        }

        if (node->role == REDIS_ROLE_MASTER &&
            node_old->role == REDIS_ROLE_MASTER) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "two masters have the same name");
            return REDIS_ERR;
        } else if (node->role == REDIS_ROLE_MASTER &&
                   node_old->role == REDIS_ROLE_SLAVE) {
            if (node->slaves == NULL) {
                node->slaves = listCreate();
                if (node->slaves == NULL) {
                    goto oom;
                }

                node->slaves->free = listClusterNodeDestructor;
            }

            if (node_old->slaves != NULL) {
                node_old->slaves->free = NULL;
                while (listLength(node_old->slaves) > 0) {
                    lnode = listFirst(node_old->slaves);
                    if (listAddNodeHead(node->slaves, lnode->value) == NULL) {
                        goto oom;
                    }
                    listDelNode(node_old->slaves, lnode);
                }
                listRelease(node_old->slaves);
                node_old->slaves = NULL;
            }

            if (listAddNodeHead(node->slaves, node_old) == NULL) {
                goto oom;
            }
            dictSetHashVal(*nodes, di, node);

        } else if (node->role == REDIS_ROLE_SLAVE) {
            if (node_old->slaves == NULL) {
                node_old->slaves = listCreate();
                if (node_old->slaves == NULL) {
                    goto oom;
                }

                node_old->slaves->free = listClusterNodeDestructor;
            }
            if (listAddNodeTail(node_old->slaves, node) == NULL) {
                goto oom;
            }

        } else {
            NOT_REACHED();
        }
    }

    return REDIS_OK;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    return REDIS_ERR;
}

/**
 * Parse the "cluster slots" command reply to nodes dict.
 */
dict *parse_cluster_slots(redisClusterContext *cc, redisReply *reply,
                          int flags) {
    int ret;
    cluster_slot *slot = NULL;
    dict *nodes = NULL;
    dictEntry *den;
    redisReply *elem_slots;
    redisReply *elem_slots_begin, *elem_slots_end;
    redisReply *elem_nodes;
    redisReply *elem_ip, *elem_port;
    redisClusterNode *master = NULL, *slave;
    uint32_t i, idx;

    if (reply == NULL) {
        return NULL;
    }

    nodes = dictCreate(&clusterNodesDictType, NULL);
    if (nodes == NULL) {
        goto oom;
    }

    if (reply->type != REDIS_REPLY_ARRAY || reply->elements <= 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "Command(cluster slots) reply error: "
                               "reply is not an array.");
        goto error;
    }

    for (i = 0; i < reply->elements; i++) {
        elem_slots = reply->element[i];
        if (elem_slots->type != REDIS_REPLY_ARRAY || elem_slots->elements < 3) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "Command(cluster slots) reply error: "
                                   "first sub_reply is not an array.");
            goto error;
        }

        slot = cluster_slot_create(NULL);
        if (slot == NULL) {
            goto oom;
        }

        // one slots region
        for (idx = 0; idx < elem_slots->elements; idx++) {
            if (idx == 0) {
                elem_slots_begin = elem_slots->element[idx];
                if (elem_slots_begin->type != REDIS_REPLY_INTEGER) {
                    __redisClusterSetError(
                        cc, REDIS_ERR_OTHER,
                        "Command(cluster slots) reply error: "
                        "slot begin is not an integer.");
                    goto error;
                }
                slot->start = (int)(elem_slots_begin->integer);
            } else if (idx == 1) {
                elem_slots_end = elem_slots->element[idx];
                if (elem_slots_end->type != REDIS_REPLY_INTEGER) {
                    __redisClusterSetError(
                        cc, REDIS_ERR_OTHER,
                        "Command(cluster slots) reply error: "
                        "slot end is not an integer.");
                    goto error;
                }

                slot->end = (int)(elem_slots_end->integer);

                if (slot->start > slot->end) {
                    __redisClusterSetError(
                        cc, REDIS_ERR_OTHER,
                        "Command(cluster slots) reply error: "
                        "slot begin is bigger than slot end.");
                    goto error;
                }
            } else {
                elem_nodes = elem_slots->element[idx];
                if (elem_nodes->type != REDIS_REPLY_ARRAY ||
                    elem_nodes->elements < 2) {
                    __redisClusterSetError(
                        cc, REDIS_ERR_OTHER,
                        "Command(cluster slots) reply error: "
                        "nodes sub_reply is not an correct array.");
                    goto error;
                }

                elem_ip = elem_nodes->element[0];
                elem_port = elem_nodes->element[1];

                if (elem_ip == NULL || elem_port == NULL ||
                    elem_ip->type != REDIS_REPLY_STRING ||
                    elem_port->type != REDIS_REPLY_INTEGER) {
                    __redisClusterSetError(
                        cc, REDIS_ERR_OTHER,
                        "Command(cluster slots) reply error: "
                        "master ip or port is not correct.");
                    goto error;
                }

                // this is master.
                if (idx == 2) {
                    sds address = sdsnewlen(elem_ip->str, elem_ip->len);
                    if (address == NULL) {
                        goto oom;
                    }
                    address = sdscatfmt(address, ":%i", elem_port->integer);
                    if (address == NULL) {
                        goto oom;
                    }

                    den = dictFind(nodes, address);
                    sdsfree(address);
                    // master already exists, break to the next slots region.
                    if (den != NULL) {

                        master = dictGetEntryVal(den);
                        ret = cluster_slot_ref_node(slot, master);
                        if (ret != REDIS_OK) {
                            goto oom;
                        }

                        slot = NULL;
                        break;
                    }

                    master = node_get_with_slots(cc, elem_ip, elem_port,
                                                 REDIS_ROLE_MASTER);
                    if (master == NULL) {
                        goto error;
                    }

                    sds key = sdsnewlen(master->addr, sdslen(master->addr));
                    if (key == NULL) {
                        freeRedisClusterNode(master);
                        goto oom;
                    }

                    ret = dictAdd(nodes, key, master);
                    if (ret != DICT_OK) {
                        sdsfree(key);
                        freeRedisClusterNode(master);
                        goto oom;
                    }

                    ret = cluster_slot_ref_node(slot, master);
                    if (ret != REDIS_OK) {
                        goto oom;
                    }

                    slot = NULL;
                } else if (flags & HIRCLUSTER_FLAG_ADD_SLAVE) {
                    slave = node_get_with_slots(cc, elem_ip, elem_port,
                                                REDIS_ROLE_SLAVE);
                    if (slave == NULL) {
                        goto error;
                    }

                    if (master->slaves == NULL) {
                        master->slaves = listCreate();
                        if (master->slaves == NULL) {
                            freeRedisClusterNode(slave);
                            goto oom;
                        }

                        master->slaves->free = listClusterNodeDestructor;
                    }

                    if (listAddNodeTail(master->slaves, slave) == NULL) {
                        freeRedisClusterNode(slave);
                        goto oom;
                    }
                }
            }
        }
    }

    return nodes;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    if (nodes != NULL) {
        dictRelease(nodes);
    }
    if (slot != NULL) {
        cluster_slot_destroy(slot);
    }
    return NULL;
}

/**
 * Parse the "cluster nodes" command reply to nodes dict.
 */
dict *parse_cluster_nodes(redisClusterContext *cc, char *str, int str_len,
                          int flags) {
    int ret;
    dict *nodes = NULL;
    dict *nodes_name = NULL;
    redisClusterNode *master, *slave;
    cluster_slot *slot;
    char *pos, *start, *end, *line_start, *line_end;
    char *role;
    int role_len;
    int slot_start, slot_end, slot_ranges_found = 0;
    sds *part = NULL, *slot_start_end = NULL;
    int count_part = 0, count_slot_start_end = 0;
    int k;
    int len;

    nodes = dictCreate(&clusterNodesDictType, NULL);
    if (nodes == NULL) {
        goto oom;
    }

    start = str;
    end = start + str_len;

    line_start = start;

    for (pos = start; pos < end; pos++) {
        if (*pos == '\n') {
            line_end = pos - 1;
            len = line_end - line_start;

            part = sdssplitlen(line_start, len + 1, " ", 1, &count_part);
            if (part == NULL) {
                goto oom;
            }

            if (count_part < 8) {
                __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                       "split cluster nodes error");
                goto error;
            }

            // if the address string starts with ":0", skip this node.
            if (sdslen(part[1]) >= 2 && memcmp(part[1], ":0", 2) == 0) {
                sdsfreesplitres(part, count_part);
                count_part = 0;
                part = NULL;

                start = pos + 1;
                line_start = start;
                pos = start;

                continue;
            }

            if (sdslen(part[2]) >= 7 && memcmp(part[2], "myself,", 7) == 0) {
                role_len = sdslen(part[2]) - 7;
                role = part[2] + 7;
            } else {
                role_len = sdslen(part[2]);
                role = part[2];
            }

            // add master node
            if (role_len >= 6 && memcmp(role, "master", 6) == 0) {
                master = node_get_with_nodes(cc, part, count_part,
                                             REDIS_ROLE_MASTER);
                if (master == NULL) {
                    goto error;
                }

                sds key = sdsnewlen(master->addr, sdslen(master->addr));
                if (key == NULL) {
                    freeRedisClusterNode(master);
                    goto oom;
                }

                ret = dictAdd(nodes, key, master);
                if (ret != DICT_OK) {
                    // Key already exists, but possibly an OOM error
                    __redisClusterSetError(
                        cc, REDIS_ERR_OTHER,
                        "The address already exists in the nodes");
                    sdsfree(key);
                    freeRedisClusterNode(master);
                    goto error;
                }

                if (flags & HIRCLUSTER_FLAG_ADD_SLAVE) {
                    ret = cluster_master_slave_mapping_with_name(
                        cc, &nodes_name, master, master->name);
                    if (ret != REDIS_OK) {
                        freeRedisClusterNode(master);
                        goto error;
                    }
                }

                for (k = 8; k < count_part; k++) {
                    slot_start_end = sdssplitlen(part[k], sdslen(part[k]), "-",
                                                 1, &count_slot_start_end);
                    if (slot_start_end == NULL) {
                        goto oom;
                    }

                    if (count_slot_start_end == 1) {
                        slot_start = hi_atoi(slot_start_end[0],
                                             sdslen(slot_start_end[0]));
                        slot_end = slot_start;
                    } else if (count_slot_start_end == 2) {
                        slot_start = hi_atoi(slot_start_end[0],
                                             sdslen(slot_start_end[0]));
                        ;
                        slot_end = hi_atoi(slot_start_end[1],
                                           sdslen(slot_start_end[1]));
                        ;
                    } else {
                        // add open slot for master
                        if (flags & HIRCLUSTER_FLAG_ADD_OPENSLOT &&
                            count_slot_start_end == 3 &&
                            sdslen(slot_start_end[0]) > 1 &&
                            sdslen(slot_start_end[1]) == 1 &&
                            sdslen(slot_start_end[2]) > 1 &&
                            slot_start_end[0][0] == '[' &&
                            slot_start_end[2][sdslen(slot_start_end[2]) - 1] ==
                                ']') {

                            copen_slot *oslot, **oslot_elem;

                            sdsrange(slot_start_end[0], 1, -1);
                            sdsrange(slot_start_end[2], 0, -2);

                            if (slot_start_end[1][0] == '>') {
                                oslot = cluster_open_slot_create(
                                    hi_atoi(slot_start_end[0],
                                            sdslen(slot_start_end[0])),
                                    1, slot_start_end[2], master);
                                if (oslot == NULL) {
                                    __redisClusterSetError(
                                        cc, REDIS_ERR_OTHER,
                                        "create open slot error");
                                    goto error;
                                }

                                if (master->migrating == NULL) {
                                    master->migrating =
                                        hiarray_create(1, sizeof(oslot));
                                    if (master->migrating == NULL) {
                                        cluster_open_slot_destroy(oslot);
                                        goto oom;
                                    }
                                }

                                oslot_elem = hiarray_push(master->migrating);
                                if (oslot_elem == NULL) {
                                    cluster_open_slot_destroy(oslot);
                                    goto oom;
                                }

                                *oslot_elem = oslot;
                            } else if (slot_start_end[1][0] == '<') {
                                oslot = cluster_open_slot_create(
                                    hi_atoi(slot_start_end[0],
                                            sdslen(slot_start_end[0])),
                                    0, slot_start_end[2], master);
                                if (oslot == NULL) {
                                    __redisClusterSetError(
                                        cc, REDIS_ERR_OTHER,
                                        "create open slot error");
                                    goto error;
                                }

                                if (master->importing == NULL) {
                                    master->importing =
                                        hiarray_create(1, sizeof(oslot));
                                    if (master->importing == NULL) {
                                        cluster_open_slot_destroy(oslot);
                                        goto oom;
                                    }
                                }

                                oslot_elem = hiarray_push(master->importing);
                                if (oslot_elem == NULL) {
                                    cluster_open_slot_destroy(oslot);
                                    goto oom;
                                }

                                *oslot_elem = oslot;
                            }
                        }

                        slot_start = -1;
                        slot_end = -1;
                    }

                    sdsfreesplitres(slot_start_end, count_slot_start_end);
                    count_slot_start_end = 0;
                    slot_start_end = NULL;

                    if (slot_start < 0 || slot_end < 0 ||
                        slot_start > slot_end ||
                        slot_end >= REDIS_CLUSTER_SLOTS) {
                        continue;
                    }
                    slot_ranges_found += 1;

                    slot = cluster_slot_create(master);
                    if (slot == NULL) {
                        goto oom;
                    }

                    slot->start = (uint32_t)slot_start;
                    slot->end = (uint32_t)slot_end;
                }

            }
            // add slave node
            else if ((flags & HIRCLUSTER_FLAG_ADD_SLAVE) &&
                     (role_len >= 5 && memcmp(role, "slave", 5) == 0)) {
                slave =
                    node_get_with_nodes(cc, part, count_part, REDIS_ROLE_SLAVE);
                if (slave == NULL) {
                    goto error;
                }

                ret = cluster_master_slave_mapping_with_name(cc, &nodes_name,
                                                             slave, part[3]);
                if (ret != REDIS_OK) {
                    freeRedisClusterNode(slave);
                    goto error;
                }
            }

            sdsfreesplitres(part, count_part);
            count_part = 0;
            part = NULL;

            start = pos + 1;
            line_start = start;
            pos = start;
        }
    }

    if (slot_ranges_found == 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "No slot information");
        goto error;
    }

    if (nodes_name != NULL) {
        dictRelease(nodes_name);
    }

    return nodes;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    sdsfreesplitres(part, count_part);
    sdsfreesplitres(slot_start_end, count_slot_start_end);
    if (nodes != NULL) {
        dictRelease(nodes);
    }
    if (nodes_name != NULL) {
        dictRelease(nodes_name);
    }
    return NULL;
}

/* Sends CLUSTER SLOTS or CLUSTER NODES to the node with context c. */
static int clusterUpdateRouteSendCommand(redisClusterContext *cc,
                                         redisContext *c) {
    const char *cmd = (cc->flags & HIRCLUSTER_FLAG_ROUTE_USE_SLOTS ?
                           REDIS_COMMAND_CLUSTER_SLOTS :
                           REDIS_COMMAND_CLUSTER_NODES);
    if (redisAppendCommand(c, cmd) != REDIS_OK) {
        const char *msg = (cc->flags & HIRCLUSTER_FLAG_ROUTE_USE_SLOTS ?
                               "Command (cluster slots) send error." :
                               "Command (cluster nodes) send error.");
        __redisClusterSetError(cc, c->err, msg);
        return REDIS_ERR;
    }
    /* Flush buffer to socket. */
    if (redisBufferWrite(c, NULL) == REDIS_ERR)
        return REDIS_ERR;

    return REDIS_OK;
}

/* Receives and handles a CLUSTER SLOTS reply from node with context c. */
static int handleClusterSlotsReply(redisClusterContext *cc, redisContext *c) {
    redisReply *reply = NULL;
    int result = redisGetReply(c, (void **)&reply);
    if (result != REDIS_OK) {
        if (c->err == REDIS_ERR_TIMEOUT) {
            __redisClusterSetError(
                cc, c->err,
                "Command (cluster slots) reply error (socket timeout)");
        } else {
            __redisClusterSetError(
                cc, REDIS_ERR_OTHER,
                "Command (cluster slots) reply error (NULL).");
        }
        return REDIS_ERR;
    } else if (reply->type != REDIS_REPLY_ARRAY) {
        if (reply->type == REDIS_REPLY_ERROR) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER, reply->str);
        } else {
            __redisClusterSetError(
                cc, REDIS_ERR_OTHER,
                "Command (cluster slots) reply error: type is not array.");
        }
        freeReplyObject(reply);
        return REDIS_ERR;
    }

    dict *nodes = parse_cluster_slots(cc, reply, cc->flags);
    freeReplyObject(reply);
    return updateNodesAndSlotmap(cc, nodes);
}

/* Receives and handles a CLUSTER NODES reply from node with context c. */
static int handleClusterNodesReply(redisClusterContext *cc, redisContext *c) {
    redisReply *reply = NULL;
    int result = redisGetReply(c, (void **)&reply);
    if (result != REDIS_OK) {
        if (c->err == REDIS_ERR_TIMEOUT) {
            __redisClusterSetError(cc, c->err,
                                   "Command (cluster nodes) reply error "
                                   "(socket timeout)");
        } else {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "Command (cluster nodes) reply error "
                                   "(NULL).");
        }
        return REDIS_ERR;
    } else if (reply->type != REDIS_REPLY_STRING) {
        if (reply->type == REDIS_REPLY_ERROR) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER, reply->str);
        } else {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "Command(cluster nodes) reply error: "
                                   "type is not string.");
        }
        freeReplyObject(reply);
        return REDIS_ERR;
    }

    dict *nodes = parse_cluster_nodes(cc, reply->str, reply->len, cc->flags);
    freeReplyObject(reply);
    return updateNodesAndSlotmap(cc, nodes);
}

/* Receives and handles a CLUSTER SLOTS or CLUSTER NODES reply from node with
 * context c. */
static int clusterUpdateRouteHandleReply(redisClusterContext *cc,
                                         redisContext *c) {
    if (cc->flags & HIRCLUSTER_FLAG_ROUTE_USE_SLOTS) {
        return handleClusterSlotsReply(cc, c);
    } else {
        return handleClusterNodesReply(cc, c);
    }
}

/**
 * Update route with the "cluster nodes" or "cluster slots" command reply.
 */
static int cluster_update_route_by_addr(redisClusterContext *cc, const char *ip,
                                        int port) {
    redisContext *c = NULL;

    if (cc == NULL) {
        return REDIS_ERR;
    }

    if (ip == NULL || port <= 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "Ip or port error!");
        goto error;
    }

    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    options.connect_timeout = cc->connect_timeout;
    options.command_timeout = cc->command_timeout;

    c = redisConnectWithOptions(&options);
    if (c == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    }

    if (cc->on_connect) {
        cc->on_connect(c, c->err ? REDIS_ERR : REDIS_OK);
    }

    if (c->err) {
        __redisClusterSetError(cc, c->err, c->errstr);
        goto error;
    }

    if (cc->ssl && cc->ssl_init_fn(c, cc->ssl) != REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        goto error;
    }

    if (authenticate(cc, c) != REDIS_OK) {
        goto error;
    }

    if (clusterUpdateRouteSendCommand(cc, c) != REDIS_OK) {
        goto error;
    }

    if (clusterUpdateRouteHandleReply(cc, c) != REDIS_OK) {
        goto error;
    }

    redisFree(c);
    return REDIS_OK;

error:
    redisFree(c);
    return REDIS_ERR;
}

/* Update known cluster nodes with a new collection of redisClusterNodes.
 * Will also update the slot-to-node lookup table for the new nodes. */
static int updateNodesAndSlotmap(redisClusterContext *cc, dict *nodes) {
    if (nodes == NULL) {
        return REDIS_ERR;
    }

    /* Create a slot to redisClusterNode lookup table */
    redisClusterNode **table;
    table = hi_calloc(REDIS_CLUSTER_SLOTS, sizeof(redisClusterNode *));
    if (table == NULL) {
        goto oom;
    }

    dictIterator di;
    dictInitIterator(&di, nodes);

    dictEntry *de;
    while ((de = dictNext(&di))) {
        redisClusterNode *master = dictGetEntryVal(de);
        if (master->role != REDIS_ROLE_MASTER) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "Node role must be master");
            goto error;
        }

        if (master->slots == NULL) {
            continue;
        }

        listIter li;
        listRewind(master->slots, &li);

        listNode *ln;
        while ((ln = listNext(&li))) {
            cluster_slot *slot = listNodeValue(ln);
            if (slot->start > slot->end || slot->end >= REDIS_CLUSTER_SLOTS) {
                __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                       "Slot region for node is invalid");
                goto error;
            }
            for (uint32_t i = slot->start; i <= slot->end; i++) {
                if (table[i] != NULL) {
                    __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                           "Different node holds same slot");
                    goto error;
                }
                table[i] = master;
            }
        }
    }

    /* Update slot-to-node table before changing cc->nodes since
     * removal of nodes might trigger user callbacks which may
     * send commands, which depend on the slot-to-node table. */
    if (cc->table != NULL) {
        hi_free(cc->table);
    }
    cc->table = table;

    cc->route_version++;

    // Move all hiredis contexts in cc->nodes to nodes
    cluster_nodes_swap_ctx(cc->nodes, nodes);

    /* Replace cc->nodes before releasing the old dict since
     * the release procedure might access cc->nodes. */
    dict *oldnodes = cc->nodes;
    cc->nodes = nodes;
    if (oldnodes != NULL) {
        dictRelease(oldnodes);
    }
    if (cc->event_callback != NULL) {
        cc->event_callback(cc, HIRCLUSTER_EVENT_SLOTMAP_UPDATED,
                           cc->event_privdata);
        if (cc->route_version == 1) {
            /* Special event the first time the slotmap was updated. */
            cc->event_callback(cc, HIRCLUSTER_EVENT_READY, cc->event_privdata);
        }
    }
    cc->need_update_route = 0;
    return REDIS_OK;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough
error:
    hi_free(table);
    dictRelease(nodes);
    return REDIS_ERR;
}

int redisClusterUpdateSlotmap(redisClusterContext *cc) {
    int ret;
    int flag_err_not_set = 1;
    redisClusterNode *node;
    dictEntry *de;

    if (cc == NULL) {
        return REDIS_ERR;
    }

    if (cc->nodes == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "no server address");
        return REDIS_ERR;
    }

    dictIterator di;
    dictInitIterator(&di, cc->nodes);

    while ((de = dictNext(&di)) != NULL) {
        node = dictGetEntryVal(de);
        if (node == NULL || node->host == NULL) {
            continue;
        }

        ret = cluster_update_route_by_addr(cc, node->host, node->port);
        if (ret == REDIS_OK) {
            if (cc->err) {
                cc->err = 0;
                memset(cc->errstr, '\0', strlen(cc->errstr));
            }
            return REDIS_OK;
        }

        flag_err_not_set = 0;
    }

    if (flag_err_not_set) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "no valid server address");
    }

    return REDIS_ERR;
}

redisClusterContext *redisClusterContextInit(void) {
    redisClusterContext *cc;

    cc = hi_calloc(1, sizeof(redisClusterContext));
    if (cc == NULL)
        return NULL;

    cc->max_retry_count = CLUSTER_DEFAULT_MAX_RETRY_COUNT;
    return cc;
}

void redisClusterFree(redisClusterContext *cc) {

    if (cc == NULL)
        return;

    if (cc->event_callback) {
        cc->event_callback(cc, HIRCLUSTER_EVENT_FREE_CONTEXT,
                           cc->event_privdata);
    }

    if (cc->connect_timeout) {
        hi_free(cc->connect_timeout);
        cc->connect_timeout = NULL;
    }

    if (cc->command_timeout) {
        hi_free(cc->command_timeout);
        cc->command_timeout = NULL;
    }

    if (cc->table != NULL) {
        hi_free(cc->table);
        cc->table = NULL;
    }

    if (cc->nodes != NULL) {
        /* Clear cc->nodes before releasing the dict since the release procedure
           might access cc->nodes. When a node and its hiredis context are freed
           all pending callbacks are executed. Clearing cc->nodes prevents a pending
           slotmap update command callback to trigger additional slotmap updates. */
        dict *nodes = cc->nodes;
        cc->nodes = NULL;
        dictRelease(nodes);
    }

    if (cc->requests != NULL) {
        listRelease(cc->requests);
    }

    if (cc->username != NULL) {
        hi_free(cc->username);
        cc->username = NULL;
    }

    if (cc->password != NULL) {
        hi_free(cc->password);
        cc->password = NULL;
    }

    hi_free(cc);
}

/* Connect to a Redis cluster. On error the field error in the returned
 * context will be set to the return value of the error function.
 * When no set of reply functions is given, the default set will be used. */
static int _redisClusterConnect2(redisClusterContext *cc) {

    if (cc->nodes == NULL || dictSize(cc->nodes) == 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "servers address does not set up");
        return REDIS_ERR;
    }

    return redisClusterUpdateSlotmap(cc);
}

/* Connect to a Redis cluster. On error the field error in the returned
 * context will be set to the return value of the error function.
 * When no set of reply functions is given, the default set will be used. */
static redisClusterContext *_redisClusterConnect(redisClusterContext *cc,
                                                 const char *addrs) {

    int ret;

    ret = redisClusterSetOptionAddNodes(cc, addrs);
    if (ret != REDIS_OK) {
        return cc;
    }

    redisClusterUpdateSlotmap(cc);

    return cc;
}

redisClusterContext *redisClusterConnect(const char *addrs, int flags) {
    redisClusterContext *cc;

    cc = redisClusterContextInit();

    if (cc == NULL) {
        return NULL;
    }

    cc->flags = flags;

    return _redisClusterConnect(cc, addrs);
}

redisClusterContext *redisClusterConnectWithTimeout(const char *addrs,
                                                    const struct timeval tv,
                                                    int flags) {
    redisClusterContext *cc;

    cc = redisClusterContextInit();

    if (cc == NULL) {
        return NULL;
    }

    cc->flags = flags;

    if (cc->connect_timeout == NULL) {
        cc->connect_timeout = hi_malloc(sizeof(struct timeval));
        if (cc->connect_timeout == NULL) {
            return NULL;
        }
    }

    memcpy(cc->connect_timeout, &tv, sizeof(struct timeval));

    return _redisClusterConnect(cc, addrs);
}

int redisClusterSetOptionAddNode(redisClusterContext *cc, const char *addr) {
    dictEntry *node_entry;
    redisClusterNode *node = NULL;
    int port, ret;
    sds ip = NULL;

    if (cc == NULL) {
        return REDIS_ERR;
    }

    if (cc->nodes == NULL) {
        cc->nodes = dictCreate(&clusterNodesDictType, NULL);
        if (cc->nodes == NULL) {
            goto oom;
        }
    }

    sds addr_sds = sdsnew(addr);
    if (addr_sds == NULL) {
        goto oom;
    }
    node_entry = dictFind(cc->nodes, addr_sds);
    sdsfree(addr_sds);
    if (node_entry == NULL) {

        char *p;
        if ((p = strrchr(addr, IP_PORT_SEPARATOR)) == NULL) {
            __redisClusterSetError(
                cc, REDIS_ERR_OTHER,
                "server address is incorrect, port separator missing.");
            return REDIS_ERR;
        }
        // p includes separator

        if (p - addr <= 0) { /* length until separator */
            __redisClusterSetError(
                cc, REDIS_ERR_OTHER,
                "server address is incorrect, address part missing.");
            return REDIS_ERR;
        }

        ip = sdsnewlen(addr, p - addr);
        if (ip == NULL) {
            goto oom;
        }
        p++; // remove separator character

        if (strlen(p) <= 0) {
            __redisClusterSetError(
                cc, REDIS_ERR_OTHER,
                "server address is incorrect, port part missing.");
            goto error;
        }

        port = hi_atoi(p, strlen(p));
        if (port <= 0) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "server port is incorrect");
            goto error;
        }

        node = createRedisClusterNode();
        if (node == NULL) {
            goto oom;
        }

        node->addr = sdsnew(addr);
        if (node->addr == NULL) {
            goto oom;
        }

        node->host = ip;
        node->port = port;

        sds key = sdsnewlen(node->addr, sdslen(node->addr));
        if (key == NULL) {
            goto oom;
        }
        ret = dictAdd(cc->nodes, key, node);
        if (ret != DICT_OK) {
            sdsfree(key);
            goto oom;
        }
    }

    return REDIS_OK;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    sdsfree(ip);
    if (node != NULL) {
        sdsfree(node->addr);
        hi_free(node);
    }
    return REDIS_ERR;
}

int redisClusterSetOptionAddNodes(redisClusterContext *cc, const char *addrs) {
    int ret;
    sds *address = NULL;
    int address_count = 0;
    int i;

    if (cc == NULL) {
        return REDIS_ERR;
    }

    address = sdssplitlen(addrs, strlen(addrs), CLUSTER_ADDRESS_SEPARATOR,
                          strlen(CLUSTER_ADDRESS_SEPARATOR), &address_count);
    if (address == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    }

    if (address_count <= 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "invalid server addresses (example format: "
                               "127.0.0.1:1234,127.0.0.2:5678)");
        sdsfreesplitres(address, address_count);
        return REDIS_ERR;
    }

    for (i = 0; i < address_count; i++) {
        ret = redisClusterSetOptionAddNode(cc, address[i]);
        if (ret != REDIS_OK) {
            sdsfreesplitres(address, address_count);
            return REDIS_ERR;
        }
    }

    sdsfreesplitres(address, address_count);

    return REDIS_OK;
}

/* Deprecated function, option has no effect. */
int redisClusterSetOptionConnectBlock(redisClusterContext *cc) {
    if (cc == NULL) {
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/* Deprecated function, option has no effect. */
int redisClusterSetOptionConnectNonBlock(redisClusterContext *cc) {
    if (cc == NULL) {
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/**
 * Configure a username used during authentication, see
 * the Redis AUTH command.
 * Disabled by default. Can be disabled again by providing an
 * empty string or a null pointer.
 */
int redisClusterSetOptionUsername(redisClusterContext *cc,
                                  const char *username) {
    if (cc == NULL) {
        return REDIS_ERR;
    }

    // Disabling option
    if (username == NULL || username[0] == '\0') {
        hi_free(cc->username);
        cc->username = NULL;
        return REDIS_OK;
    }

    hi_free(cc->username);
    cc->username = hi_strdup(username);
    if (cc->username == NULL) {
        return REDIS_ERR;
    }

    return REDIS_OK;
}

/**
 * Configure a password used when connecting to password-protected
 * Redis instances. (See Redis AUTH command)
 */
int redisClusterSetOptionPassword(redisClusterContext *cc,
                                  const char *password) {

    if (cc == NULL) {
        return REDIS_ERR;
    }

    // Disabling use of password
    if (password == NULL || password[0] == '\0') {
        hi_free(cc->password);
        cc->password = NULL;
        return REDIS_OK;
    }

    hi_free(cc->password);
    cc->password = hi_strdup(password);
    if (cc->password == NULL) {
        return REDIS_ERR;
    }

    return REDIS_OK;
}

int redisClusterSetOptionParseSlaves(redisClusterContext *cc) {

    if (cc == NULL) {
        return REDIS_ERR;
    }

    cc->flags |= HIRCLUSTER_FLAG_ADD_SLAVE;

    return REDIS_OK;
}

int redisClusterSetOptionParseOpenSlots(redisClusterContext *cc) {

    if (cc == NULL) {
        return REDIS_ERR;
    }

    cc->flags |= HIRCLUSTER_FLAG_ADD_OPENSLOT;

    return REDIS_OK;
}

int redisClusterSetOptionRouteUseSlots(redisClusterContext *cc) {

    if (cc == NULL) {
        return REDIS_ERR;
    }

    cc->flags |= HIRCLUSTER_FLAG_ROUTE_USE_SLOTS;

    return REDIS_OK;
}

int redisClusterSetOptionConnectTimeout(redisClusterContext *cc,
                                        const struct timeval tv) {

    if (cc == NULL) {
        return REDIS_ERR;
    }

    if (cc->connect_timeout == NULL) {
        cc->connect_timeout = hi_malloc(sizeof(struct timeval));
        if (cc->connect_timeout == NULL) {
            __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
            return REDIS_ERR;
        }
    }

    memcpy(cc->connect_timeout, &tv, sizeof(struct timeval));

    return REDIS_OK;
}

int redisClusterSetOptionTimeout(redisClusterContext *cc,
                                 const struct timeval tv) {
    if (cc == NULL) {
        return REDIS_ERR;
    }

    if (cc->command_timeout == NULL ||
        cc->command_timeout->tv_sec != tv.tv_sec ||
        cc->command_timeout->tv_usec != tv.tv_usec) {

        if (cc->command_timeout == NULL) {
            cc->command_timeout = hi_malloc(sizeof(struct timeval));
            if (cc->command_timeout == NULL) {
                __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
                return REDIS_ERR;
            }
        }

        memcpy(cc->command_timeout, &tv, sizeof(struct timeval));

        /* Set timeout on already connected nodes */
        if (cc->nodes && dictSize(cc->nodes) > 0) {
            dictEntry *de;
            redisClusterNode *node;

            dictIterator di;
            dictInitIterator(&di, cc->nodes);

            while ((de = dictNext(&di)) != NULL) {
                node = dictGetEntryVal(de);
                if (node->acon) {
                    redisAsyncSetTimeout(node->acon, tv);
                }
                if (node->con && node->con->err == 0) {
                    redisSetTimeout(node->con, tv);
                }

                if (node->slaves && listLength(node->slaves) > 0) {
                    redisClusterNode *slave;
                    listNode *ln;

                    listIter li;
                    listRewind(node->slaves, &li);

                    while ((ln = listNext(&li)) != NULL) {
                        slave = listNodeValue(ln);
                        if (slave->acon) {
                            redisAsyncSetTimeout(slave->acon, tv);
                        }
                        if (slave->con && slave->con->err == 0) {
                            redisSetTimeout(slave->con, tv);
                        }
                    }
                }
            }
        }
    }

    return REDIS_OK;
}

int redisClusterSetOptionMaxRetry(redisClusterContext *cc,
                                  int max_retry_count) {
    if (cc == NULL || max_retry_count <= 0) {
        return REDIS_ERR;
    }

    cc->max_retry_count = max_retry_count;

    return REDIS_OK;
}

int redisClusterConnect2(redisClusterContext *cc) {

    if (cc == NULL) {
        return REDIS_ERR;
    }

    return _redisClusterConnect2(cc);
}

redisContext *ctx_get_by_node(redisClusterContext *cc, redisClusterNode *node) {
    redisContext *c = NULL;
    if (node == NULL) {
        return NULL;
    }

    c = node->con;
    if (c != NULL) {
        if (c->err) {
            redisReconnect(c);

            if (cc->on_connect) {
                cc->on_connect(c, c->err ? REDIS_ERR : REDIS_OK);
            }

            if (cc->ssl && cc->ssl_init_fn(c, cc->ssl) != REDIS_OK) {
                __redisClusterSetError(cc, c->err, c->errstr);
            }

            authenticate(cc, c); // err and errstr handled in function
        }

        return c;
    }

    if (node->host == NULL || node->port <= 0) {
        return NULL;
    }

    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, node->host, node->port);
    options.connect_timeout = cc->connect_timeout;
    options.command_timeout = cc->command_timeout;

    c = redisConnectWithOptions(&options);
    if (c == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        return NULL;
    }

    if (cc->on_connect) {
        cc->on_connect(c, c->err ? REDIS_ERR : REDIS_OK);
    }

    if (c->err) {
        __redisClusterSetError(cc, c->err, c->errstr);
        redisFree(c);
        return NULL;
    }

    if (cc->ssl && cc->ssl_init_fn(c, cc->ssl) != REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        redisFree(c);
        return NULL;
    }

    if (authenticate(cc, c) != REDIS_OK) {
        redisFree(c);
        return NULL;
    }

    node->con = c;

    return c;
}

static redisClusterNode *node_get_by_table(redisClusterContext *cc,
                                           uint32_t slot_num) {
    if (cc == NULL) {
        return NULL;
    }

    if (slot_num >= REDIS_CLUSTER_SLOTS) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "invalid slot");
        return NULL;
    }

    if (cc->table == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "slotmap not available");
        return NULL;
    }

    if (cc->table[slot_num] == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "slot not served by any node");
        return NULL;
    }

    return cc->table[slot_num];
}

/* Helper function for the redisClusterAppendCommand* family of functions.
 *
 * Write a formatted command to the output buffer. When this family
 * is used, you need to call redisGetReply yourself to retrieve
 * the reply (or replies in pub/sub).
 */
static int __redisClusterAppendCommand(redisClusterContext *cc,
                                       struct cmd *command) {

    redisClusterNode *node;
    redisContext *c = NULL;

    if (cc == NULL || command == NULL) {
        return REDIS_ERR;
    }

    node = node_get_by_table(cc, (uint32_t)command->slot_num);
    if (node == NULL) {
        return REDIS_ERR;
    }

    c = ctx_get_by_node(cc, node);
    if (c == NULL) {
        return REDIS_ERR;
    } else if (c->err) {
        __redisClusterSetError(cc, c->err, c->errstr);
        return REDIS_ERR;
    }

    if (redisAppendFormattedCommand(c, command->cmd, command->clen) !=
        REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        return REDIS_ERR;
    }

    return REDIS_OK;
}

/* Helper functions for the redisClusterGetReply* family of functions.
 */
static int __redisClusterGetReplyFromNode(redisClusterContext *cc,
                                          redisClusterNode *node,
                                          void **reply) {
    redisContext *c;

    if (cc == NULL || node == NULL || reply == NULL)
        return REDIS_ERR;

    c = node->con;
    if (c == NULL) {
        return REDIS_ERR;
    } else if (c->err) {
        if (cc->need_update_route == 0) {
            cc->retry_count++;
            if (cc->retry_count > cc->max_retry_count) {
                cc->need_update_route = 1;
                cc->retry_count = 0;
            }
        }
        __redisClusterSetError(cc, c->err, c->errstr);
        return REDIS_ERR;
    }

    if (redisGetReply(c, reply) != REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        return REDIS_ERR;
    }

    if (cluster_reply_error_type(*reply) == CLUSTER_ERR_MOVED)
        cc->need_update_route = 1;

    return REDIS_OK;
}

static int __redisClusterGetReply(redisClusterContext *cc, int slot_num,
                                  void **reply) {
    redisClusterNode *node;

    if (cc == NULL || slot_num < 0 || reply == NULL)
        return REDIS_ERR;

    node = node_get_by_table(cc, (uint32_t)slot_num);
    if (node == NULL) {
        return REDIS_ERR;
    }

    return __redisClusterGetReplyFromNode(cc, node, reply);
}

/* Parses a MOVED or ASK error reply and returns the destination node. The slot
 * is returned by pointer, if provided. */
static redisClusterNode *getNodeFromRedirectReply(redisClusterContext *cc,
                                                  redisReply *reply,
                                                  int *slotptr) {
    redisClusterNode *node = NULL;
    sds *part = NULL;
    int part_len = 0;
    char *p;

    /* Expecting ["ASK" | "MOVED", "<slot>", "<endpoint>:<port>"] */
    part = sdssplitlen(reply->str, reply->len, " ", 1, &part_len);
    if (part == NULL) {
        goto oom;
    }
    if (part_len != 3) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "failed to parse redirect");
        goto done;
    }

    /* Parse slot if requested. */
    if (slotptr != NULL) {
        *slotptr = hi_atoi(part[1], sdslen(part[1]));
    }

    /* Find the last occurance of the port separator since
     * IPv6 addresses can contain ':' */
    if ((p = strrchr(part[2], IP_PORT_SEPARATOR)) == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "port separator missing in redirect");
        goto done;
    }
    // p includes separator

    /* Empty endpoint not supported yet */
    if (p - part[2] == 0) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "endpoint missing in redirect");
        goto done;
    }

    dictEntry *de = dictFind(cc->nodes, part[2]);
    if (de != NULL) {
        node = de->val;
        goto done;
    }

    /* Add this node since it was unknown */
    node = createRedisClusterNode();
    if (node == NULL) {
        goto oom;
    }
    node->role = REDIS_ROLE_MASTER;
    node->addr = part[2];
    part[2] = NULL; /* Memory ownership moved */

    node->host = sdsnewlen(node->addr, p - node->addr);
    if (node->host == NULL) {
        goto oom;
    }
    p++; // remove found separator character
    node->port = hi_atoi(p, strlen(p));

    sds key = sdsnewlen(node->addr, sdslen(node->addr));
    if (key == NULL) {
        goto oom;
    }

    if (dictAdd(cc->nodes, key, node) != DICT_OK) {
        sdsfree(key);
        goto oom;
    }

done:
    sdsfreesplitres(part, part_len);
    return node;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    sdsfreesplitres(part, part_len);
    if (node != NULL) {
        sdsfree(node->addr);
        sdsfree(node->host);
        hi_free(node);
    }

    return NULL;
}

static void *redis_cluster_command_execute(redisClusterContext *cc,
                                           struct cmd *command) {
    void *reply = NULL;
    redisClusterNode *node;
    redisContext *c = NULL;
    int error_type;
    redisContext *c_updating_route = NULL;

retry:

    node = node_get_by_table(cc, (uint32_t)command->slot_num);
    if (node == NULL) {
        /* Update the slotmap since the slot is not served. */
        if (redisClusterUpdateSlotmap(cc) != REDIS_OK) {
            goto error;
        }
        node = node_get_by_table(cc, (uint32_t)command->slot_num);
        if (node == NULL) {
            /* Return error since the slot is still not served. */
            goto error;
        }
    }

    c = ctx_get_by_node(cc, node);
    if (c == NULL || c->err) {
        /* Failed to connect. Maybe there was a failover and this node is gone.
         * Update slotmap to find out. */
        if (redisClusterUpdateSlotmap(cc) != REDIS_OK) {
            goto error;
        }

        node = node_get_by_table(cc, (uint32_t)command->slot_num);
        if (node == NULL) {
            goto error;
        }
        c = ctx_get_by_node(cc, node);
        if (c == NULL) {
            goto error;
        } else if (c->err) {
            __redisClusterSetError(cc, c->err, c->errstr);
            goto error;
        }
    }

moved_retry:
ask_retry:

    if (redisAppendFormattedCommand(c, command->cmd, command->clen) !=
        REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        goto error;
    }

    /* If update slotmap has been scheduled, do that in the same pipeline. */
    if (cc->need_update_route && c_updating_route == NULL) {
        if (clusterUpdateRouteSendCommand(cc, c) == REDIS_OK) {
            c_updating_route = c;
        }
    }

    if (redisGetReply(c, &reply) != REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        /* We may need to update the slotmap if this node is removed from the
         * cluster, but the current request may have already timed out so we
         * schedule it for later. */
        if (c->err != REDIS_ERR_OOM)
            cc->need_update_route = 1;
        goto error;
    }

    error_type = cluster_reply_error_type(reply);
    if (error_type > CLUSTER_NOT_ERR && error_type < CLUSTER_ERR_SENTINEL) {
        cc->retry_count++;
        if (cc->retry_count > cc->max_retry_count) {
            __redisClusterSetError(cc, REDIS_ERR_CLUSTER_TOO_MANY_RETRIES,
                                   "too many cluster retries");
            goto error;
        }

        int slot = -1;
        switch (error_type) {
        case CLUSTER_ERR_MOVED:
            node = getNodeFromRedirectReply(cc, reply, &slot);
            freeReplyObject(reply);
            reply = NULL;

            if (node == NULL) {
                /* Failed to parse redirect. Specific error already set. */
                goto error;
            }

            /* Update the slot mapping entry for this slot. */
            if (slot >= 0) {
                cc->table[slot] = node;
            }

            if (c_updating_route == NULL) {
                if (clusterUpdateRouteSendCommand(cc, c) == REDIS_OK) {
                    /* Deferred update route using the node that sent the
                     * redirect. */
                    c_updating_route = c;
                } else if (redisClusterUpdateSlotmap(cc) == REDIS_OK) {
                    /* Synchronous update route successful using new connection. */
                    cc->err = 0;
                    cc->errstr[0] = '\0';
                } else {
                    /* Failed to update route. Specific error already set. */
                    goto error;
                }
            }

            c = ctx_get_by_node(cc, node);
            if (c == NULL) {
                goto error;
            } else if (c->err) {
                __redisClusterSetError(cc, c->err, c->errstr);
                goto error;
            }

            goto moved_retry;

            break;
        case CLUSTER_ERR_ASK:
            node = getNodeFromRedirectReply(cc, reply, NULL);
            if (node == NULL) {
                goto error;
            }

            freeReplyObject(reply);
            reply = NULL;

            c = ctx_get_by_node(cc, node);
            if (c == NULL) {
                goto error;
            } else if (c->err) {
                __redisClusterSetError(cc, c->err, c->errstr);
                goto error;
            }

            reply = redisCommand(c, REDIS_COMMAND_ASKING);
            if (reply == NULL) {
                __redisClusterSetError(cc, c->err, c->errstr);
                goto error;
            }

            freeReplyObject(reply);
            reply = NULL;

            goto ask_retry;

            break;
        case CLUSTER_ERR_TRYAGAIN:
        case CLUSTER_ERR_CLUSTERDOWN:
            freeReplyObject(reply);
            reply = NULL;
            goto retry;

            break;
        default:

            break;
        }
    }

    goto done;

error:
    if (reply) {
        freeReplyObject(reply);
        reply = NULL;
    }

done:
    if (c_updating_route) {
        /* Deferred CLUSTER SLOTS or CLUSTER NODES in progress. Wait for the
         * reply and handle it. */
        if (clusterUpdateRouteHandleReply(cc, c_updating_route) != REDIS_OK) {
            /* Clear error and update synchronously using another node. */
            cc->err = 0;
            cc->errstr[0] = '\0';
            if (redisClusterUpdateSlotmap(cc) != REDIS_OK) {
                /* Clear the reply to indicate failure. */
                freeReplyObject(reply);
                reply = NULL;
            }
        }
    }

    return reply;
}

static int command_pre_fragment(redisClusterContext *cc, struct cmd *command,
                                hilist *commands) {

    struct keypos *kp, *sub_kp;
    uint32_t key_count;
    uint32_t i, j;
    uint32_t idx;
    uint32_t key_len;
    int slot_num = -1;
    struct cmd *sub_command;
    struct cmd **sub_commands = NULL;
    char num_str[12];
    uint8_t num_str_len;

    if (command == NULL || commands == NULL) {
        goto done;
    }

    key_count = hiarray_n(command->keys);

    sub_commands = hi_calloc(REDIS_CLUSTER_SLOTS, sizeof(*sub_commands));
    if (sub_commands == NULL) {
        goto oom;
    }

    command->frag_seq = hi_calloc(key_count, sizeof(*command->frag_seq));
    if (command->frag_seq == NULL) {
        goto oom;
    }

    // Fill sub_command with key, slot and command length (clen, only keylength)
    for (i = 0; i < key_count; i++) {
        kp = hiarray_get(command->keys, i);

        slot_num = keyHashSlot(kp->start, kp->end - kp->start);

        if (slot_num < 0 || slot_num >= REDIS_CLUSTER_SLOTS) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "keyHashSlot return error");
            goto done;
        }

        if (sub_commands[slot_num] == NULL) {
            sub_commands[slot_num] = command_get();
            if (sub_commands[slot_num] == NULL) {
                goto oom;
            }
        }

        command->frag_seq[i] = sub_command = sub_commands[slot_num];

        sub_command->narg++;

        sub_kp = hiarray_push(sub_command->keys);
        if (sub_kp == NULL) {
            goto oom;
        }

        sub_kp->start = kp->start;
        sub_kp->end = kp->end;

        // Number of characters in key
        key_len = (uint32_t)(kp->end - kp->start);

        sub_command->clen += key_len + uint_len(key_len);

        sub_command->slot_num = slot_num;

        if (command->type == CMD_REQ_REDIS_MSET) {
            uint32_t len = 0;
            char *p;

            for (p = sub_kp->end + 1; !isdigit(*p); p++) {
            }

            p = sub_kp->end + 1;
            while (!isdigit(*p)) {
                p++;
            }

            for (; isdigit(*p); p++) {
                len = len * 10 + (uint32_t)(*p - '0');
            }

            len += CRLF_LEN * 2;
            len += (p - sub_kp->end);
            sub_kp->remain_len = len;
            sub_command->clen += len;
        }
    }

    /* prepend command header */
    for (i = 0; i < REDIS_CLUSTER_SLOTS; i++) {
        sub_command = sub_commands[i];
        if (sub_command == NULL) {
            continue;
        }

        idx = 0;
        if (command->type == CMD_REQ_REDIS_MGET) {
            //"*%d\r\n$4\r\nmget\r\n"

            sub_command->clen += 5 * sub_command->narg;

            sub_command->narg++;

            hi_itoa(num_str, sub_command->narg);
            num_str_len = (uint8_t)(strlen(num_str));

            sub_command->clen += 13 + num_str_len;

            sub_command->cmd =
                hi_calloc(sub_command->clen, sizeof(*sub_command->cmd));
            if (sub_command->cmd == NULL) {
                goto oom;
            }

            sub_command->cmd[idx++] = '*';
            memcpy(sub_command->cmd + idx, num_str, num_str_len);
            idx += num_str_len;
            memcpy(sub_command->cmd + idx, "\r\n$4\r\nmget\r\n", 12);
            idx += 12;

            for (j = 0; j < hiarray_n(sub_command->keys); j++) {
                kp = hiarray_get(sub_command->keys, j);
                key_len = (uint32_t)(kp->end - kp->start);
                hi_itoa(num_str, key_len);
                num_str_len = strlen(num_str);

                sub_command->cmd[idx++] = '$';
                memcpy(sub_command->cmd + idx, num_str, num_str_len);
                idx += num_str_len;
                memcpy(sub_command->cmd + idx, CRLF, CRLF_LEN);
                idx += CRLF_LEN;
                memcpy(sub_command->cmd + idx, kp->start, key_len);
                idx += key_len;
                memcpy(sub_command->cmd + idx, CRLF, CRLF_LEN);
                idx += CRLF_LEN;
            }
        } else if (command->type == CMD_REQ_REDIS_DEL) {
            //"*%d\r\n$3\r\ndel\r\n"

            sub_command->clen += 5 * sub_command->narg;

            sub_command->narg++;

            hi_itoa(num_str, sub_command->narg);
            num_str_len = (uint8_t)strlen(num_str);

            sub_command->clen += 12 + num_str_len;

            sub_command->cmd =
                hi_calloc(sub_command->clen, sizeof(*sub_command->cmd));
            if (sub_command->cmd == NULL) {
                goto oom;
            }

            sub_command->cmd[idx++] = '*';
            memcpy(sub_command->cmd + idx, num_str, num_str_len);
            idx += num_str_len;
            memcpy(sub_command->cmd + idx, "\r\n$3\r\ndel\r\n", 11);
            idx += 11;

            for (j = 0; j < hiarray_n(sub_command->keys); j++) {
                kp = hiarray_get(sub_command->keys, j);
                key_len = (uint32_t)(kp->end - kp->start);
                hi_itoa(num_str, key_len);
                num_str_len = strlen(num_str);

                sub_command->cmd[idx++] = '$';
                memcpy(sub_command->cmd + idx, num_str, num_str_len);
                idx += num_str_len;
                memcpy(sub_command->cmd + idx, CRLF, CRLF_LEN);
                idx += CRLF_LEN;
                memcpy(sub_command->cmd + idx, kp->start, key_len);
                idx += key_len;
                memcpy(sub_command->cmd + idx, CRLF, CRLF_LEN);
                idx += CRLF_LEN;
            }
        } else if (command->type == CMD_REQ_REDIS_EXISTS) {
            //"*%d\r\n$6\r\nexists\r\n"

            sub_command->clen += 5 * sub_command->narg;

            sub_command->narg++;

            hi_itoa(num_str, sub_command->narg);
            num_str_len = (uint8_t)strlen(num_str);

            sub_command->clen += 15 + num_str_len;

            sub_command->cmd =
                hi_calloc(sub_command->clen, sizeof(*sub_command->cmd));
            if (sub_command->cmd == NULL) {
                goto oom;
            }

            sub_command->cmd[idx++] = '*';
            memcpy(sub_command->cmd + idx, num_str, num_str_len);
            idx += num_str_len;
            memcpy(sub_command->cmd + idx, "\r\n$6\r\nexists\r\n", 14);
            idx += 14;

            for (j = 0; j < hiarray_n(sub_command->keys); j++) {
                kp = hiarray_get(sub_command->keys, j);
                key_len = (uint32_t)(kp->end - kp->start);
                hi_itoa(num_str, key_len);
                num_str_len = strlen(num_str);

                sub_command->cmd[idx++] = '$';
                memcpy(sub_command->cmd + idx, num_str, num_str_len);
                idx += num_str_len;
                memcpy(sub_command->cmd + idx, CRLF, CRLF_LEN);
                idx += CRLF_LEN;
                memcpy(sub_command->cmd + idx, kp->start, key_len);
                idx += key_len;
                memcpy(sub_command->cmd + idx, CRLF, CRLF_LEN);
                idx += CRLF_LEN;
            }
        } else if (command->type == CMD_REQ_REDIS_MSET) {
            //"*%d\r\n$4\r\nmset\r\n"

            sub_command->clen += 3 * sub_command->narg;

            sub_command->narg *= 2;

            sub_command->narg++;

            hi_itoa(num_str, sub_command->narg);
            num_str_len = (uint8_t)strlen(num_str);

            sub_command->clen += 13 + num_str_len;

            sub_command->cmd =
                hi_calloc(sub_command->clen, sizeof(*sub_command->cmd));
            if (sub_command->cmd == NULL) {
                goto oom;
            }

            sub_command->cmd[idx++] = '*';
            memcpy(sub_command->cmd + idx, num_str, num_str_len);
            idx += num_str_len;
            memcpy(sub_command->cmd + idx, "\r\n$4\r\nmset\r\n", 12);
            idx += 12;

            for (j = 0; j < hiarray_n(sub_command->keys); j++) {
                kp = hiarray_get(sub_command->keys, j);
                key_len = (uint32_t)(kp->end - kp->start);
                hi_itoa(num_str, key_len);
                num_str_len = strlen(num_str);

                sub_command->cmd[idx++] = '$';
                memcpy(sub_command->cmd + idx, num_str, num_str_len);
                idx += num_str_len;
                memcpy(sub_command->cmd + idx, CRLF, CRLF_LEN);
                idx += CRLF_LEN;
                memcpy(sub_command->cmd + idx, kp->start,
                       key_len + kp->remain_len);
                idx += key_len + kp->remain_len;
            }
        } else {
            NOT_REACHED();
        }

        sub_command->type = command->type;

        if (listAddNodeTail(commands, sub_command) == NULL) {
            goto oom;
        }
        sub_commands[i] = NULL;
    }

done:
    hi_free(sub_commands);

    if (slot_num >= 0 && commands != NULL && listLength(commands) == 1) {
        listNode *list_node = listFirst(commands);
        listDelNode(commands, list_node);
        if (command->frag_seq) {
            hi_free(command->frag_seq);
            command->frag_seq = NULL;
        }

        command->slot_num = slot_num;
    }
    return slot_num;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    if (sub_commands != NULL) {
        for (i = 0; i < REDIS_CLUSTER_SLOTS; i++) {
            command_destroy(sub_commands[i]);
        }
    }
    hi_free(sub_commands);
    return -1; // failing slot_num
}

static void *command_post_fragment(redisClusterContext *cc, struct cmd *command,
                                   hilist *commands) {
    struct cmd *sub_command;
    listNode *list_node;
    redisReply *reply = NULL, *sub_reply;
    long long count = 0;

    listIter li;
    listRewind(commands, &li);

    while ((list_node = listNext(&li)) != NULL) {
        sub_command = list_node->value;
        reply = sub_command->reply;
        if (reply == NULL) {
            return NULL;
        } else if (reply->type == REDIS_REPLY_ERROR) {
            return reply;
        }

        if (command->type == CMD_REQ_REDIS_MGET) {
            if (reply->type != REDIS_REPLY_ARRAY) {
                __redisClusterSetError(cc, REDIS_ERR_OTHER, "reply type error");
                return NULL;
            }
        } else if (command->type == CMD_REQ_REDIS_DEL) {
            if (reply->type != REDIS_REPLY_INTEGER) {
                __redisClusterSetError(cc, REDIS_ERR_OTHER, "reply type error");
                return NULL;
            }
            count += reply->integer;
        } else if (command->type == CMD_REQ_REDIS_EXISTS) {
            if (reply->type != REDIS_REPLY_INTEGER) {
                __redisClusterSetError(cc, REDIS_ERR_OTHER, "reply type error");
                return NULL;
            }
            count += reply->integer;
        } else if (command->type == CMD_REQ_REDIS_MSET) {
            if (reply->type != REDIS_REPLY_STATUS || reply->len != 2 ||
                strcmp(reply->str, REDIS_STATUS_OK) != 0) {
                __redisClusterSetError(cc, REDIS_ERR_OTHER, "reply type error");
                return NULL;
            }
        } else {
            NOT_REACHED();
        }
    }

    reply = hi_calloc(1, sizeof(*reply));
    if (reply == NULL) {
        goto oom;
    }

    if (command->type == CMD_REQ_REDIS_MGET) {
        int i;
        uint32_t key_count;

        reply->type = REDIS_REPLY_ARRAY;

        key_count = hiarray_n(command->keys);

        reply->elements = key_count;
        reply->element = hi_calloc(key_count, sizeof(*reply->element));
        if (reply->element == NULL) {
            goto oom;
        }

        for (i = key_count - 1; i >= 0; i--) {       /* for each key */
            sub_reply = command->frag_seq[i]->reply; /* get it's reply */
            if (sub_reply == NULL) {
                freeReplyObject(reply);
                __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                       "sub reply is null");
                return NULL;
            }

            if (sub_reply->type == REDIS_REPLY_STRING) {
                reply->element[i] = sub_reply;
            } else if (sub_reply->type == REDIS_REPLY_ARRAY) {
                if (sub_reply->elements == 0) {
                    freeReplyObject(reply);
                    __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                           "sub reply elements error");
                    return NULL;
                }

                reply->element[i] = sub_reply->element[sub_reply->elements - 1];
                sub_reply->elements--;
            }
        }
    } else if (command->type == CMD_REQ_REDIS_DEL) {
        reply->type = REDIS_REPLY_INTEGER;
        reply->integer = count;
    } else if (command->type == CMD_REQ_REDIS_EXISTS) {
        reply->type = REDIS_REPLY_INTEGER;
        reply->integer = count;
    } else if (command->type == CMD_REQ_REDIS_MSET) {
        reply->type = REDIS_REPLY_STATUS;
        uint32_t str_len = strlen(REDIS_STATUS_OK);
        reply->str = hi_malloc((str_len + 1) * sizeof(char));
        if (reply->str == NULL) {
            goto oom;
        }

        reply->len = str_len;
        memcpy(reply->str, REDIS_STATUS_OK, str_len);
        reply->str[str_len] = '\0';
    } else {
        NOT_REACHED();
    }

    return reply;

oom:
    freeReplyObject(reply);
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    return NULL;
}

/*
 * Split the command into subcommands by slot
 *
 * Returns slot_num
 * If slot_num < 0 or slot_num >=  REDIS_CLUSTER_SLOTS means this function runs
 * error; Otherwise if  the commands > 1 , slot_num is the last subcommand slot
 * number.
 */
static int command_format_by_slot(redisClusterContext *cc, struct cmd *command,
                                  hilist *commands) {
    struct keypos *kp;
    int key_count;
    int slot_num = -1;

    if (cc == NULL || commands == NULL || command == NULL ||
        command->cmd == NULL || command->clen <= 0) {
        goto done;
    }

    redis_parse_cmd(command);
    if (command->result == CMD_PARSE_ENOMEM) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        goto done;
    } else if (command->result != CMD_PARSE_OK) {
        __redisClusterSetError(cc, REDIS_ERR_PROTOCOL, command->errstr);
        goto done;
    }

    key_count = hiarray_n(command->keys);

    if (key_count <= 0) {
        __redisClusterSetError(
            cc, REDIS_ERR_OTHER,
            "No keys in command(must have keys for redis cluster mode)");
        goto done;
    } else if (key_count == 1) {
        kp = hiarray_get(command->keys, 0);
        slot_num = keyHashSlot(kp->start, kp->end - kp->start);
        command->slot_num = slot_num;

        goto done;
    }

    slot_num = command_pre_fragment(cc, command, commands);

done:

    return slot_num;
}

/* Deprecated function, replaced with redisClusterSetOptionMaxRetry() */
void redisClusterSetMaxRedirect(redisClusterContext *cc, int max_retry_count) {
    if (cc == NULL || max_retry_count <= 0) {
        return;
    }

    cc->max_retry_count = max_retry_count;
}

int redisClusterSetConnectCallback(redisClusterContext *cc,
                                   void(fn)(const redisContext *c,
                                            int status)) {
    if (cc->on_connect == NULL) {
        cc->on_connect = fn;
        return REDIS_OK;
    }
    return REDIS_ERR;
}

int redisClusterSetEventCallback(redisClusterContext *cc,
                                 void(fn)(const redisClusterContext *cc,
                                          int event, void *privdata),
                                 void *privdata) {
    if (cc->event_callback == NULL) {
        cc->event_callback = fn;
        cc->event_privdata = privdata;
        return REDIS_OK;
    }
    return REDIS_ERR;
}

void *redisClusterFormattedCommand(redisClusterContext *cc, char *cmd,
                                   int len) {
    redisReply *reply = NULL;
    int slot_num;
    struct cmd *command = NULL, *sub_command;
    hilist *commands = NULL;
    listNode *list_node;

    if (cc == NULL) {
        return NULL;
    }

    if (cc->err) {
        cc->err = 0;
        memset(cc->errstr, '\0', strlen(cc->errstr));
    }

    command = command_get();
    if (command == NULL) {
        goto oom;
    }

    command->cmd = cmd;
    command->clen = len;

    commands = listCreate();
    if (commands == NULL) {
        goto oom;
    }

    commands->free = listCommandFree;

    slot_num = command_format_by_slot(cc, command, commands);

    if (slot_num < 0) {
        goto error;
    } else if (slot_num >= REDIS_CLUSTER_SLOTS) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "slot_num is out of range");
        goto error;
    }

    // all keys belong to one slot
    if (listLength(commands) == 0) {
        reply = redis_cluster_command_execute(cc, command);
        goto done;
    }

    ASSERT(listLength(commands) != 1);

    listIter li;
    listRewind(commands, &li);

    while ((list_node = listNext(&li)) != NULL) {
        sub_command = list_node->value;

        reply = redis_cluster_command_execute(cc, sub_command);
        if (reply == NULL) {
            goto error;
        } else if (reply->type == REDIS_REPLY_ERROR) {
            goto done;
        }

        sub_command->reply = reply;
    }

    reply = command_post_fragment(cc, command, commands);

done:

    command->cmd = NULL;
    command_destroy(command);

    if (commands != NULL) {
        listRelease(commands);
    }

    cc->retry_count = 0;
    return reply;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    if (command != NULL) {
        command->cmd = NULL;
        command_destroy(command);
    }
    if (commands != NULL) {
        listRelease(commands);
    }
    cc->retry_count = 0;
    return NULL;
}

void *redisClustervCommand(redisClusterContext *cc, const char *format,
                           va_list ap) {
    redisReply *reply;
    char *cmd;
    int len;

    if (cc == NULL) {
        return NULL;
    }

    len = redisvFormatCommand(&cmd, format, ap);

    if (len == -1) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        return NULL;
    } else if (len == -2) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "Invalid format string");
        return NULL;
    }

    reply = redisClusterFormattedCommand(cc, cmd, len);

    hi_free(cmd);

    return reply;
}

void *redisClusterCommand(redisClusterContext *cc, const char *format, ...) {
    va_list ap;
    redisReply *reply = NULL;

    va_start(ap, format);
    reply = redisClustervCommand(cc, format, ap);
    va_end(ap);

    return reply;
}

void *redisClusterCommandToNode(redisClusterContext *cc, redisClusterNode *node,
                                const char *format, ...) {
    redisContext *c;
    va_list ap;
    int ret;
    void *reply;
    int updating_slotmap = 0;

    c = ctx_get_by_node(cc, node);
    if (c == NULL) {
        return NULL;
    } else if (c->err) {
        __redisClusterSetError(cc, c->err, c->errstr);
        return NULL;
    }

    if (cc->err) {
        cc->err = 0;
        memset(cc->errstr, '\0', sizeof(cc->errstr));
    }

    va_start(ap, format);
    ret = redisvAppendCommand(c, format, ap);
    va_end(ap);

    if (ret != REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        return NULL;
    }

    if (cc->need_update_route) {
        /* Pipeline slotmap update on the same connection. */
        if (clusterUpdateRouteSendCommand(cc, c) == REDIS_OK) {
            updating_slotmap = 1;
        }
    }

    if (redisGetReply(c, &reply) != REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        if (c->err != REDIS_ERR_OOM)
            cc->need_update_route = 1;
        return NULL;
    }

    if (updating_slotmap) {
        /* Handle reply from pipelined CLUSTER SLOTS or CLUSTER NODES. */
        if (clusterUpdateRouteHandleReply(cc, c) != REDIS_OK) {
            /* Ignore error. Update will be triggered on the next command. */
            cc->err = 0;
            cc->errstr[0] = '\0';
        }
    }

    return reply;
}

void *redisClusterCommandArgv(redisClusterContext *cc, int argc,
                              const char **argv, const size_t *argvlen) {
    redisReply *reply = NULL;
    char *cmd;
    int len;

    len = redisFormatCommandArgv(&cmd, argc, argv, argvlen);
    if (len == -1) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        return NULL;
    }

    reply = redisClusterFormattedCommand(cc, cmd, len);

    hi_free(cmd);

    return reply;
}

int redisClusterAppendFormattedCommand(redisClusterContext *cc, char *cmd,
                                       int len) {
    int slot_num;
    struct cmd *command = NULL, *sub_command;
    hilist *commands = NULL;
    listNode *list_node;

    if (cc->requests == NULL) {
        cc->requests = listCreate();
        if (cc->requests == NULL) {
            goto oom;
        }
        cc->requests->free = listCommandFree;
    }

    command = command_get();
    if (command == NULL) {
        goto oom;
    }

    command->cmd = cmd;
    command->clen = len;

    commands = listCreate();
    if (commands == NULL) {
        goto oom;
    }

    commands->free = listCommandFree;

    slot_num = command_format_by_slot(cc, command, commands);

    if (slot_num < 0) {
        goto error;
    } else if (slot_num >= REDIS_CLUSTER_SLOTS) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "slot_num is out of range");
        goto error;
    }

    // Append command(s)
    if (listLength(commands) == 0) {
        // All keys belong to one slot
        if (__redisClusterAppendCommand(cc, command) != REDIS_OK) {
            goto error;
        }
    } else {
        // Keys belongs to different slots
        ASSERT(listLength(commands) != 1);

        listIter li;
        listRewind(commands, &li);

        while ((list_node = listNext(&li)) != NULL) {
            sub_command = list_node->value;

            if (__redisClusterAppendCommand(cc, sub_command) != REDIS_OK) {
                goto error;
            }
        }
    }

    if (listLength(commands) > 0) {
        command->sub_commands = commands;
    } else {
        listRelease(commands);
    }
    commands = NULL;
    command->cmd = NULL;

    if (listAddNodeTail(cc->requests, command) == NULL) {
        goto oom;
    }
    return REDIS_OK;

oom:
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    if (command != NULL) {
        command->cmd = NULL;
        command_destroy(command);
    }
    if (commands != NULL) {
        listRelease(commands);
    }

    /* Attention: mybe here we must pop the
      sub_commands that had append to the nodes.
      But now we do not handle it. */
    return REDIS_ERR;
}

int redisClustervAppendCommand(redisClusterContext *cc, const char *format,
                               va_list ap) {
    int ret;
    char *cmd;
    int len;

    len = redisvFormatCommand(&cmd, format, ap);
    if (len == -1) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    } else if (len == -2) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "Invalid format string");
        return REDIS_ERR;
    }

    ret = redisClusterAppendFormattedCommand(cc, cmd, len);

    hi_free(cmd);

    return ret;
}

int redisClusterAppendCommand(redisClusterContext *cc, const char *format,
                              ...) {

    int ret;
    va_list ap;

    if (cc == NULL || format == NULL) {
        return REDIS_ERR;
    }

    va_start(ap, format);
    ret = redisClustervAppendCommand(cc, format, ap);
    va_end(ap);

    return ret;
}

int redisClusterAppendCommandToNode(redisClusterContext *cc,
                                    redisClusterNode *node, const char *format,
                                    ...) {
    redisContext *c;
    va_list ap;
    struct cmd *command = NULL;
    char *cmd = NULL;
    int len;

    if (cc->requests == NULL) {
        cc->requests = listCreate();
        if (cc->requests == NULL)
            goto oom;

        cc->requests->free = listCommandFree;
    }

    c = ctx_get_by_node(cc, node);
    if (c == NULL) {
        return REDIS_ERR;
    } else if (c->err) {
        __redisClusterSetError(cc, c->err, c->errstr);
        return REDIS_ERR;
    }

    /* Allocate cmd and encode the variadic command */
    va_start(ap, format);
    len = redisvFormatCommand(&cmd, format, ap);
    va_end(ap);

    if (len == -1) {
        goto oom;
    } else if (len == -2) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER, "Invalid format string");
        return REDIS_ERR;
    }

    // Append the command to the outgoing hiredis buffer
    if (redisAppendFormattedCommand(c, cmd, len) != REDIS_OK) {
        __redisClusterSetError(cc, c->err, c->errstr);
        hi_free(cmd);
        return REDIS_ERR;
    }

    // Keep the command in the outstanding request list
    command = command_get();
    if (command == NULL) {
        hi_free(cmd);
        goto oom;
    }
    command->cmd = cmd;
    command->clen = len;
    command->node_addr = sdsnew(node->addr);
    if (command->node_addr == NULL)
        goto oom;

    if (listAddNodeTail(cc->requests, command) == NULL)
        goto oom;

    return REDIS_OK;

oom:
    command_destroy(command);
    __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
    return REDIS_ERR;
}

int redisClusterAppendCommandArgv(redisClusterContext *cc, int argc,
                                  const char **argv, const size_t *argvlen) {
    int ret;
    char *cmd;
    int len;

    len = redisFormatCommandArgv(&cmd, argc, argv, argvlen);
    if (len == -1) {
        __redisClusterSetError(cc, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    }

    ret = redisClusterAppendFormattedCommand(cc, cmd, len);

    hi_free(cmd);

    return ret;
}

static int redisClusterSendAll(redisClusterContext *cc) {
    dictEntry *de;
    redisClusterNode *node;
    redisContext *c = NULL;
    int wdone = 0;

    if (cc == NULL || cc->nodes == NULL) {
        return REDIS_ERR;
    }

    dictIterator di;
    dictInitIterator(&di, cc->nodes);

    while ((de = dictNext(&di)) != NULL) {
        node = dictGetEntryVal(de);
        if (node == NULL) {
            continue;
        }

        c = ctx_get_by_node(cc, node);
        if (c == NULL) {
            continue;
        }

        /* Write until done */
        do {
            if (redisBufferWrite(c, &wdone) == REDIS_ERR) {
                return REDIS_ERR;
            }
        } while (!wdone);
    }

    return REDIS_OK;
}

static int redisClusterClearAll(redisClusterContext *cc) {
    dictEntry *de;
    redisClusterNode *node;
    redisContext *c = NULL;

    if (cc == NULL) {
        return REDIS_ERR;
    }

    if (cc->err) {
        cc->err = 0;
        memset(cc->errstr, '\0', strlen(cc->errstr));
    }

    if (cc->nodes == NULL) {
        return REDIS_ERR;
    }

    dictIterator di;
    dictInitIterator(&di, cc->nodes);

    while ((de = dictNext(&di)) != NULL) {
        node = dictGetEntryVal(de);
        if (node == NULL) {
            continue;
        }

        c = node->con;
        if (c == NULL) {
            continue;
        }

        redisFree(c);
        node->con = NULL;
    }

    return REDIS_OK;
}

int redisClusterGetReply(redisClusterContext *cc, void **reply) {

    struct cmd *command, *sub_command;
    hilist *commands = NULL;
    listNode *list_command, *list_sub_command;
    int slot_num;
    void *sub_reply;

    if (cc == NULL || reply == NULL)
        return REDIS_ERR;

    cc->err = 0;
    cc->errstr[0] = '\0';

    *reply = NULL;

    if (cc->requests == NULL)
        return REDIS_ERR; // No queued requests

    list_command = listFirst(cc->requests);

    // no more reply
    if (list_command == NULL) {
        *reply = NULL;
        return REDIS_OK;
    }

    command = list_command->value;
    if (command == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "command in the requests list is null");
        goto error;
    }

    slot_num = command->slot_num;
    if (slot_num >= 0) {
        /* Command was sent via single slot */
        listDelNode(cc->requests, list_command);
        return __redisClusterGetReply(cc, slot_num, reply);

    } else if (command->node_addr) {
        /* Command was sent to a single node */
        dictEntry *de;

        de = dictFind(cc->nodes, command->node_addr);
        if (de != NULL) {
            listDelNode(cc->requests, list_command);
            return __redisClusterGetReplyFromNode(cc, dictGetEntryVal(de),
                                                  reply);
        } else {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "command was sent to a now unknown node");
            goto error;
        }
    }

    commands = command->sub_commands;
    if (commands == NULL) {
        __redisClusterSetError(cc, REDIS_ERR_OTHER,
                               "sub_commands in command is null");
        goto error;
    }

    ASSERT(listLength(commands) != 1);

    listIter li;
    listRewind(commands, &li);

    while ((list_sub_command = listNext(&li)) != NULL) {
        sub_command = list_sub_command->value;
        if (sub_command == NULL) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER, "sub_command is null");
            goto error;
        }

        slot_num = sub_command->slot_num;
        if (slot_num < 0) {
            __redisClusterSetError(cc, REDIS_ERR_OTHER,
                                   "sub_command slot_num is less then zero");
            goto error;
        }

        if (__redisClusterGetReply(cc, slot_num, &sub_reply) != REDIS_OK) {
            goto error;
        }

        sub_command->reply = sub_reply;
    }

    *reply = command_post_fragment(cc, command, commands);
    if (*reply == NULL) {
        goto error;
    }

    listDelNode(cc->requests, list_command);
    return REDIS_OK;

error:

    listDelNode(cc->requests, list_command);
    return REDIS_ERR;
}

/**
 * Resets cluster state after pipeline. 
 * Resets Redis node connections if pipeline commands were not called beforehand.
 */
void redisClusterReset(redisClusterContext *cc) {
    int status;
    void *reply;

    if (cc == NULL || cc->nodes == NULL) {
        return;
    }

    if (cc->err) {
        redisClusterClearAll(cc);
    } else {
        /* Write/flush each nodes output buffer to socket */
        redisClusterSendAll(cc);

        /* Expect a reply for each pipelined request */
        do {
            status = redisClusterGetReply(cc, &reply);
            if (status == REDIS_OK) {
                freeReplyObject(reply);
            } else {
                redisClusterClearAll(cc);
                break;
            }
        } while (reply != NULL);
    }

    if (cc->requests) {
        listRelease(cc->requests);
        cc->requests = NULL;
    }

    if (cc->need_update_route) {
        status = redisClusterUpdateSlotmap(cc);
        if (status != REDIS_OK) {
            /* Specific error already set */
            return;
        }
        cc->need_update_route = 0;
    }
}

/*############redis cluster async############*/

static void __redisClusterAsyncSetError(redisClusterAsyncContext *acc, int type,
                                        const char *str) {

    size_t len;

    acc->err = type;
    if (str != NULL) {
        len = strlen(str);
        len = len < (sizeof(acc->errstr) - 1) ? len : (sizeof(acc->errstr) - 1);
        memcpy(acc->errstr, str, len);
        acc->errstr[len] = '\0';
    } else {
        /* Only REDIS_ERR_IO may lack a description! */
        assert(type == REDIS_ERR_IO);
        strerror_r(errno, acc->errstr, sizeof(acc->errstr));
    }
}

static redisClusterAsyncContext *
redisClusterAsyncInitialize(redisClusterContext *cc) {
    redisClusterAsyncContext *acc;

    if (cc == NULL) {
        return NULL;
    }

    acc = hi_calloc(1, sizeof(redisClusterAsyncContext));
    if (acc == NULL)
        return NULL;

    acc->cc = cc;

    /* We want the error field to be accessible directly instead of requiring
     * an indirection to the redisContext struct. */
    // TODO: really needed?
    acc->err = cc->err;
    memcpy(acc->errstr, cc->errstr, 128);

    return acc;
}

static cluster_async_data *cluster_async_data_create(void) {
    /* use calloc to guarantee all fields are zeroed */
    return hi_calloc(1, sizeof(cluster_async_data));
}

static void cluster_async_data_free(cluster_async_data *cad) {
    if (cad == NULL) {
        return;
    }

    command_destroy(cad->command);

    hi_free(cad);
}

static void unlinkAsyncContextAndNode(void *data) {
    redisClusterNode *node;

    if (data) {
        node = (redisClusterNode *)(data);
        node->acon = NULL;
    }
}

redisAsyncContext *actx_get_by_node(redisClusterAsyncContext *acc,
                                    redisClusterNode *node) {
    redisAsyncContext *ac;
    int ret;

    if (node == NULL) {
        return NULL;
    }

    ac = node->acon;
    if (ac != NULL) {
        if (ac->c.err == 0) {
            return ac;
        } else {
            /* The cluster node has a hiredis context with errors. Hiredis
             * will asynchronously destruct the context and unlink it from
             * the cluster node object. Return an error until done.
             * An example scenario is when sending a command from a command
             * callback, which has a NULL reply due to a disconnect. */
            __redisClusterAsyncSetError(acc, ac->c.err, ac->c.errstr);
            return NULL;
        }
    }

    // No async context exists, perform a connect

    if (node->host == NULL || node->port <= 0) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER,
                                    "node host or port is error");
        return NULL;
    }

    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, node->host, node->port);
    options.connect_timeout = acc->cc->connect_timeout;
    options.command_timeout = acc->cc->command_timeout;

    node->lastConnectionAttempt = hi_usec_now();

    ac = redisAsyncConnectWithOptions(&options);
    if (ac == NULL) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OOM, "Out of memory");
        return NULL;
    }

    if (ac->err) {
        __redisClusterAsyncSetError(acc, ac->err, ac->errstr);
        redisAsyncFree(ac);
        return NULL;
    }

    if (acc->cc->ssl &&
        acc->cc->ssl_init_fn(&ac->c, acc->cc->ssl) != REDIS_OK) {
        __redisClusterAsyncSetError(acc, ac->c.err, ac->c.errstr);
        redisAsyncFree(ac);
        return NULL;
    }

    // Authenticate when needed
    if (acc->cc->password != NULL) {
        if (acc->cc->username != NULL) {
            ret = redisAsyncCommand(ac, NULL, NULL, "AUTH %s %s",
                                    acc->cc->username, acc->cc->password);
        } else {
            ret =
                redisAsyncCommand(ac, NULL, NULL, "AUTH %s", acc->cc->password);
        }

        if (ret != REDIS_OK) {
            __redisClusterAsyncSetError(acc, ac->c.err, ac->c.errstr);
            redisAsyncFree(ac);
            return NULL;
        }
    }

    if (acc->adapter) {
        ret = acc->attach_fn(ac, acc->adapter);
        if (ret != REDIS_OK) {
            __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER,
                                        "Failed to attach event adapter");
            redisAsyncFree(ac);
            return NULL;
        }
    }

    if (acc->onConnect) {
        redisAsyncSetConnectCallback(ac, acc->onConnect);
    }
#ifndef HIRCLUSTER_NO_NONCONST_CONNECT_CB
    else if (acc->onConnectNC) {
        redisAsyncSetConnectCallbackNC(ac, acc->onConnectNC);
    }
#endif

    if (acc->onDisconnect) {
        redisAsyncSetDisconnectCallback(ac, acc->onDisconnect);
    }

    ac->data = node;
    ac->dataCleanup = unlinkAsyncContextAndNode;
    node->acon = ac;

    return ac;
}

redisClusterAsyncContext *redisClusterAsyncContextInit(void) {
    redisClusterContext *cc;
    redisClusterAsyncContext *acc;

    cc = redisClusterContextInit();
    if (cc == NULL) {
        return NULL;
    }

    acc = redisClusterAsyncInitialize(cc);
    if (acc == NULL) {
        redisClusterFree(cc);
        return NULL;
    }

    return acc;
}

redisClusterAsyncContext *redisClusterAsyncConnect(const char *addrs,
                                                   int flags) {

    redisClusterContext *cc;
    redisClusterAsyncContext *acc;

    cc = redisClusterConnect(addrs, flags);
    if (cc == NULL) {
        return NULL;
    }

    acc = redisClusterAsyncInitialize(cc);
    if (acc == NULL) {
        redisClusterFree(cc);
        return NULL;
    }

    return acc;
}

int redisClusterAsyncConnect2(redisClusterAsyncContext *acc) {
    /* An adapter to an async event library is required. */
    if (acc->adapter == NULL) {
        return REDIS_ERR;
    }
    return updateSlotMapAsync(acc, NULL /*any node*/);
}

int redisClusterAsyncSetConnectCallback(redisClusterAsyncContext *acc,
                                        redisConnectCallback *fn) {
    if (acc->onConnect != NULL)
        return REDIS_ERR;
#ifndef HIRCLUSTER_NO_NONCONST_CONNECT_CB
    if (acc->onConnectNC != NULL)
        return REDIS_ERR;
#endif
    acc->onConnect = fn;
    return REDIS_OK;
}

#ifndef HIRCLUSTER_NO_NONCONST_CONNECT_CB
int redisClusterAsyncSetConnectCallbackNC(redisClusterAsyncContext *acc,
                                          redisConnectCallbackNC *fn) {
    if (acc->onConnectNC != NULL || acc->onConnect != NULL) {
        return REDIS_ERR;
    }
    acc->onConnectNC = fn;
    return REDIS_OK;
}
#endif

int redisClusterAsyncSetDisconnectCallback(redisClusterAsyncContext *acc,
                                           redisDisconnectCallback *fn) {
    if (acc->onDisconnect == NULL) {
        acc->onDisconnect = fn;
        return REDIS_OK;
    }
    return REDIS_ERR;
}

/* Reply callback function for CLUSTER SLOTS */
void clusterSlotsReplyCallback(redisAsyncContext *ac, void *r, void *privdata) {
    UNUSED(ac);
    redisReply *reply = (redisReply *)r;
    redisClusterAsyncContext *acc = (redisClusterAsyncContext *)privdata;
    acc->lastSlotmapUpdateAttempt = hi_usec_now();

    if (reply == NULL) {
        /* Retry using available nodes */
        updateSlotMapAsync(acc, NULL);
        return;
    }

    redisClusterContext *cc = acc->cc;
    dict *nodes = parse_cluster_slots(cc, reply, cc->flags);
    if (updateNodesAndSlotmap(cc, nodes) != REDIS_OK) {
        /* Ignore failures for now */
    }
}

/* Reply callback function for CLUSTER NODES */
void clusterNodesReplyCallback(redisAsyncContext *ac, void *r, void *privdata) {
    UNUSED(ac);
    redisReply *reply = (redisReply *)r;
    redisClusterAsyncContext *acc = (redisClusterAsyncContext *)privdata;
    acc->lastSlotmapUpdateAttempt = hi_usec_now();

    if (reply == NULL) {
        /* Retry using available nodes */
        updateSlotMapAsync(acc, NULL);
        return;
    }

    redisClusterContext *cc = acc->cc;
    dict *nodes = parse_cluster_nodes(cc, reply->str, reply->len, cc->flags);
    if (updateNodesAndSlotmap(cc, nodes) != REDIS_OK) {
        /* Ignore failures for now */
    }
}

#define nodeIsConnected(n)                                                     \
    ((n)->acon != NULL && (n)->acon->err == 0 &&                               \
     (n)->acon->c.flags & REDIS_CONNECTED)

/* Select a node.
 * Primarily selects a connected node found close to a randomly picked index of
 * all known nodes. The random index should give a more even distribution of
 * selected nodes. If no connected node is found while iterating to this index
 * the remaining nodes are also checked until a connected node is found.
 * If no connected node is found a node for which a connect has not been attempted
 * within throttle-time, and is found near the picked index, is selected.
 */
static redisClusterNode *selectNode(dict *nodes) {
    redisClusterNode *node, *selected = NULL;
    dictIterator di;
    dictInitIterator(&di, nodes);

    int64_t throttleLimit = hi_usec_now() - SLOTMAP_UPDATE_THROTTLE_USEC;
    unsigned long currentIndex = 0;
    unsigned long checkIndex = random() % dictSize(nodes);

    dictEntry *de;
    while ((de = dictNext(&di)) != NULL) {
        node = dictGetEntryVal(de);

        if (nodeIsConnected(node)) {
            /* Keep any connected node */
            selected = node;
        } else if (node->lastConnectionAttempt < throttleLimit &&
                   (selected == NULL || (currentIndex < checkIndex &&
                                         !nodeIsConnected(selected)))) {
            /* Keep an accepted node when none is yet found, or
               any accepted node until the chosen index is reached */
            selected = node;
        }

        /* Return a found connected node when chosen index is reached. */
        if (currentIndex >= checkIndex && selected != NULL &&
            nodeIsConnected(selected))
            break;
        currentIndex += 1;
    }
    return selected;
}

/* Update the slot map by querying a selected cluster node. If ac is NULL, an
 * arbitrary connected node is selected. */
static int updateSlotMapAsync(redisClusterAsyncContext *acc,
                              redisAsyncContext *ac) {
    if (acc->lastSlotmapUpdateAttempt == SLOTMAP_UPDATE_ONGOING) {
        /* Don't allow concurrent slot map updates. */
        return REDIS_ERR;
    }

    if (ac == NULL) {
        if (acc->cc->nodes == NULL) {
            __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER, "no nodes added");
            goto error;
        }

        redisClusterNode *node = selectNode(acc->cc->nodes);
        if (node == NULL) {
            goto error;
        }

        /* Get hiredis context, connect if needed */
        ac = actx_get_by_node(acc, node);
    }
    if (ac == NULL)
        goto error; /* Specific error already set */

    /* Send a command depending of config */
    int status;
    if (acc->cc->flags & HIRCLUSTER_FLAG_ROUTE_USE_SLOTS) {
        status = redisAsyncCommand(ac, clusterSlotsReplyCallback, acc,
                                   REDIS_COMMAND_CLUSTER_SLOTS);
    } else {
        status = redisAsyncCommand(ac, clusterNodesReplyCallback, acc,
                                   REDIS_COMMAND_CLUSTER_NODES);
    }

    if (status == REDIS_OK) {
        acc->lastSlotmapUpdateAttempt = SLOTMAP_UPDATE_ONGOING;
        return REDIS_OK;
    }

error:
    acc->lastSlotmapUpdateAttempt = hi_usec_now();
    return REDIS_ERR;
}

/* Start a slotmap update if the throttling allows. */
static void throttledUpdateSlotMapAsync(redisClusterAsyncContext *acc,
                                        redisAsyncContext *ac) {
    if (acc->lastSlotmapUpdateAttempt != SLOTMAP_UPDATE_ONGOING &&
        (acc->lastSlotmapUpdateAttempt + SLOTMAP_UPDATE_THROTTLE_USEC) <
            hi_usec_now()) {
        updateSlotMapAsync(acc, ac);
    }
}

static void redisClusterAsyncCallback(redisAsyncContext *ac, void *r,
                                      void *privdata) {
    int ret;
    redisReply *reply = r;
    cluster_async_data *cad = privdata;
    redisClusterAsyncContext *acc;
    redisClusterContext *cc;
    redisAsyncContext *ac_retry = NULL;
    int error_type;
    redisClusterNode *node;
    struct cmd *command;

    if (cad == NULL) {
        goto error;
    }

    acc = cad->acc;
    if (acc == NULL) {
        goto error;
    }

    cc = acc->cc;
    if (cc == NULL) {
        goto error;
    }

    command = cad->command;
    if (command == NULL) {
        goto error;
    }

    if (reply == NULL) {
        /* Copy reply specific error from hiredis */
        __redisClusterAsyncSetError(acc, ac->err, ac->errstr);

        node = (redisClusterNode *)ac->data;
        if (node == NULL)
            goto done; /* Node already removed from topology */

        /* Start a slotmap update when the throttling allows */
        throttledUpdateSlotMapAsync(acc, NULL);
        goto done;
    }

    if (cad->retry_count == NO_RETRY) /* Skip retry handling */
        goto done;

    error_type = cluster_reply_error_type(reply);

    if (error_type > CLUSTER_NOT_ERR && error_type < CLUSTER_ERR_SENTINEL) {
        cad->retry_count++;
        if (cad->retry_count > cc->max_retry_count) {
            cad->retry_count = 0;
            __redisClusterAsyncSetError(acc, REDIS_ERR_CLUSTER_TOO_MANY_RETRIES,
                                        "too many cluster retries");
            goto done;
        }

        int slot = -1;
        switch (error_type) {
        case CLUSTER_ERR_MOVED:
            /* Initiate slot mapping update using the node that sent MOVED. */
            throttledUpdateSlotMapAsync(acc, ac);

            node = getNodeFromRedirectReply(cc, reply, &slot);
            if (node == NULL) {
                __redisClusterAsyncSetError(acc, cc->err, cc->errstr);
                goto done;
            }
            /* Update the slot mapping entry for this slot. */
            if (slot >= 0) {
                cc->table[slot] = node;
            }
            ac_retry = actx_get_by_node(acc, node);

            break;
        case CLUSTER_ERR_ASK:
            node = getNodeFromRedirectReply(cc, reply, NULL);
            if (node == NULL) {
                __redisClusterAsyncSetError(acc, cc->err, cc->errstr);
                goto done;
            }

            ac_retry = actx_get_by_node(acc, node);
            if (ac_retry == NULL) {
                /* Specific error already set */
                goto done;
            }

            ret = redisAsyncCommand(ac_retry, NULL, NULL, REDIS_COMMAND_ASKING);
            if (ret != REDIS_OK) {
                goto error;
            }

            break;
        case CLUSTER_ERR_TRYAGAIN:
        case CLUSTER_ERR_CLUSTERDOWN:
            ac_retry = ac;

            break;
        default:

            goto done;
            break;
        }

        goto retry;
    }

done:

    if (acc->err) {
        cad->callback(acc, NULL, cad->privdata);
    } else {
        cad->callback(acc, r, cad->privdata);
    }

    if (cc->err) {
        cc->err = 0;
        memset(cc->errstr, '\0', strlen(cc->errstr));
    }

    if (acc->err) {
        acc->err = 0;
        memset(acc->errstr, '\0', strlen(acc->errstr));
    }

    cluster_async_data_free(cad);

    return;

retry:

    ret = redisAsyncFormattedCommand(ac_retry, redisClusterAsyncCallback, cad,
                                     command->cmd, command->clen);
    if (ret != REDIS_OK) {
        goto error;
    }

    return;

error:

    cluster_async_data_free(cad);
}

int redisClusterAsyncFormattedCommand(redisClusterAsyncContext *acc,
                                      redisClusterCallbackFn *fn,
                                      void *privdata, char *cmd, int len) {

    redisClusterContext *cc;
    int status = REDIS_OK;
    int slot_num;
    redisClusterNode *node;
    redisAsyncContext *ac;
    struct cmd *command = NULL;
    hilist *commands = NULL;
    cluster_async_data *cad;

    if (acc == NULL) {
        return REDIS_ERR;
    }

    cc = acc->cc;

    if (cc->err) {
        cc->err = 0;
        memset(cc->errstr, '\0', strlen(cc->errstr));
    }

    if (acc->err) {
        acc->err = 0;
        memset(acc->errstr, '\0', strlen(acc->errstr));
    }

    command = command_get();
    if (command == NULL) {
        goto oom;
    }

    command->cmd = hi_calloc(len, sizeof(*command->cmd));
    if (command->cmd == NULL) {
        goto oom;
    }
    memcpy(command->cmd, cmd, len);
    command->clen = len;

    commands = listCreate();
    if (commands == NULL) {
        goto oom;
    }

    commands->free = listCommandFree;

    slot_num = command_format_by_slot(cc, command, commands);

    if (slot_num < 0) {
        __redisClusterAsyncSetError(acc, cc->err, cc->errstr);
        goto error;
    } else if (slot_num >= REDIS_CLUSTER_SLOTS) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER,
                                    "slot_num is out of range");
        goto error;
    }

    // all keys not belong to one slot
    if (listLength(commands) > 0) {
        ASSERT(listLength(commands) != 1);

        __redisClusterAsyncSetError(
            acc, REDIS_ERR_OTHER,
            "Asynchronous API now not support multi-key command");
        goto error;
    }

    node = node_get_by_table(cc, (uint32_t)slot_num);
    if (node == NULL) {
        /* Initiate a slotmap update since the slot is not served. */
        throttledUpdateSlotMapAsync(acc, NULL);

        /* node_get_by_table() has set the error on cc. */
        __redisClusterAsyncSetError(acc, cc->err, cc->errstr);
        goto error;
    }

    ac = actx_get_by_node(acc, node);
    if (ac == NULL) {
        /* Specific error already set */
        goto error;
    }

    cad = cluster_async_data_create();
    if (cad == NULL) {
        goto oom;
    }

    cad->acc = acc;
    cad->command = command;
    cad->callback = fn;
    cad->privdata = privdata;

    status = redisAsyncFormattedCommand(ac, redisClusterAsyncCallback, cad, cmd,
                                        len);
    if (status != REDIS_OK) {
        goto error;
    }

    if (commands != NULL) {
        listRelease(commands);
    }

    return REDIS_OK;

oom:
    __redisClusterAsyncSetError(acc, REDIS_ERR_OOM, "Out of memory");
    // passthrough

error:
    command_destroy(command);
    if (commands != NULL) {
        listRelease(commands);
    }
    return REDIS_ERR;
}

int redisClusterAsyncFormattedCommandToNode(redisClusterAsyncContext *acc,
                                            redisClusterNode *node,
                                            redisClusterCallbackFn *fn,
                                            void *privdata, char *cmd,
                                            int len) {
    redisClusterContext *cc;
    redisAsyncContext *ac;
    int status;
    cluster_async_data *cad = NULL;
    struct cmd *command = NULL;

    ac = actx_get_by_node(acc, node);
    if (ac == NULL) {
        /* Specific error already set */
        return REDIS_ERR;
    }

    cc = acc->cc;

    if (cc->err) {
        cc->err = 0;
        memset(cc->errstr, '\0', strlen(cc->errstr));
    }

    if (acc->err) {
        acc->err = 0;
        memset(acc->errstr, '\0', strlen(acc->errstr));
    }

    command = command_get();
    if (command == NULL) {
        goto oom;
    }

    command->cmd = hi_calloc(len, sizeof(*command->cmd));
    if (command->cmd == NULL) {
        goto oom;
    }
    memcpy(command->cmd, cmd, len);
    command->clen = len;

    cad = cluster_async_data_create();
    if (cad == NULL)
        goto oom;

    cad->acc = acc;
    cad->command = command;
    cad->callback = fn;
    cad->privdata = privdata;
    cad->retry_count = NO_RETRY;

    status = redisAsyncFormattedCommand(ac, redisClusterAsyncCallback, cad, cmd,
                                        len);
    if (status != REDIS_OK)
        goto error;

    return REDIS_OK;

oom:
    __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER, "Out of memory");
    // passthrough

error:
    command_destroy(command);
    return REDIS_ERR;
}

int redisClustervAsyncCommand(redisClusterAsyncContext *acc,
                              redisClusterCallbackFn *fn, void *privdata,
                              const char *format, va_list ap) {
    int ret;
    char *cmd;
    int len;

    if (acc == NULL) {
        return REDIS_ERR;
    }

    len = redisvFormatCommand(&cmd, format, ap);
    if (len == -1) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    } else if (len == -2) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER,
                                    "Invalid format string");
        return REDIS_ERR;
    }

    ret = redisClusterAsyncFormattedCommand(acc, fn, privdata, cmd, len);

    hi_free(cmd);

    return ret;
}

int redisClusterAsyncCommand(redisClusterAsyncContext *acc,
                             redisClusterCallbackFn *fn, void *privdata,
                             const char *format, ...) {
    int ret;
    va_list ap;

    va_start(ap, format);
    ret = redisClustervAsyncCommand(acc, fn, privdata, format, ap);
    va_end(ap);

    return ret;
}

int redisClusterAsyncCommandToNode(redisClusterAsyncContext *acc,
                                   redisClusterNode *node,
                                   redisClusterCallbackFn *fn, void *privdata,
                                   const char *format, ...) {
    int ret;
    va_list ap;
    int len;
    char *cmd = NULL;

    /* Allocate cmd and encode the variadic command */
    va_start(ap, format);
    len = redisvFormatCommand(&cmd, format, ap);
    va_end(ap);

    if (len == -1) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER, "Out of memory");
        return REDIS_ERR;
    } else if (len == -2) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OTHER,
                                    "Invalid format string");
        return REDIS_ERR;
    }

    ret = redisClusterAsyncFormattedCommandToNode(acc, node, fn, privdata, cmd,
                                                  len);
    hi_free(cmd);
    return ret;
}

int redisClusterAsyncCommandArgv(redisClusterAsyncContext *acc,
                                 redisClusterCallbackFn *fn, void *privdata,
                                 int argc, const char **argv,
                                 const size_t *argvlen) {
    int ret;
    char *cmd;
    int len;

    len = redisFormatCommandArgv(&cmd, argc, argv, argvlen);
    if (len == -1) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    }

    ret = redisClusterAsyncFormattedCommand(acc, fn, privdata, cmd, len);

    hi_free(cmd);

    return ret;
}

int redisClusterAsyncCommandArgvToNode(redisClusterAsyncContext *acc,
                                       redisClusterNode *node,
                                       redisClusterCallbackFn *fn,
                                       void *privdata, int argc,
                                       const char **argv,
                                       const size_t *argvlen) {

    int ret;
    char *cmd;
    int len;

    len = redisFormatCommandArgv(&cmd, argc, argv, argvlen);
    if (len == -1) {
        __redisClusterAsyncSetError(acc, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    }

    ret = redisClusterAsyncFormattedCommandToNode(acc, node, fn, privdata, cmd,
                                                  len);

    hi_free(cmd);

    return ret;
}

void redisClusterAsyncDisconnect(redisClusterAsyncContext *acc) {
    redisClusterContext *cc;
    redisAsyncContext *ac;
    dictEntry *de;
    redisClusterNode *node;

    if (acc == NULL) {
        return;
    }

    cc = acc->cc;

    if (cc->nodes == NULL) {
        return;
    }

    dictIterator di;
    dictInitIterator(&di, cc->nodes);

    while ((de = dictNext(&di)) != NULL) {
        node = dictGetEntryVal(de);

        ac = node->acon;

        if (ac == NULL) {
            continue;
        }

        redisAsyncDisconnect(ac);
    }
}

void redisClusterAsyncFree(redisClusterAsyncContext *acc) {
    redisClusterContext *cc;

    if (acc == NULL) {
        return;
    }

    cc = acc->cc;

    redisClusterFree(cc);

    hi_free(acc);
}

/* Initiate an iterator for iterating over current cluster nodes */
void redisClusterInitNodeIterator(redisClusterNodeIterator *iter,
                                  redisClusterContext *cc) {
    iter->cc = cc;
    iter->route_version = cc->route_version;
    dictInitIterator(&iter->di, cc->nodes);
    iter->retries_left = 1;
}

/* Get next node from the iterator
 * The iterator will restart if the routing table is updated
 * before all nodes have been iterated. */
redisClusterNode *redisClusterNodeNext(redisClusterNodeIterator *iter) {
    if (iter->retries_left <= 0)
        return NULL;

    if (iter->route_version != iter->cc->route_version) {
        // The routing table has changed and current iterator
        // is invalid. The nodes dict has been recreated in
        // the cluster context. We need to re-init the dictIter.
        dictInitIterator(&iter->di, iter->cc->nodes);
        iter->route_version = iter->cc->route_version;
        iter->retries_left--;
    }

    dictEntry *de;
    if ((de = dictNext(&iter->di)) != NULL)
        return dictGetEntryVal(de);
    else
        return NULL;
}

/* Get hash slot for given key string, which can include hash tags */
unsigned int redisClusterGetSlotByKey(char *key) {
    return keyHashSlot(key, strlen(key));
}

/* Get node that handles given key string, which can include hash tags */
redisClusterNode *redisClusterGetNodeByKey(redisClusterContext *cc, char *key) {
    return node_get_by_table(cc, keyHashSlot(key, strlen(key)));
}

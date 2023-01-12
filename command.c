/*
 * Copyright (c) 2015-2017, Ieshen Zheng <ieshen.zheng at 163 dot com>
 * Copyright (c) 2020, Nick <heronr1 at gmail dot com>
 * Copyright (c) 2020-2021, Bjorn Svensson <bjorn.a.svensson at est dot tech>
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
#include <ctype.h>
#include <errno.h>
#include <hiredis/alloc.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <string.h>

#include "command.h"
#include "hiarray.h"
#include "hiutil.h"
#include "win32.h"

#define LF (uint8_t)10
#define CR (uint8_t)13

static uint64_t cmd_id = 0; /* command id counter */

typedef struct {
    cmd_type_t type;     /* A constant identifying the command. */
    const char *name;    /* Command name */
    const char *subname; /* Subcommand name or NULL */
    int firstkey; /* Position of first key, 0 for no key, -1 for unknown */
    int arity;    /* Arity, where negative number means minimum num args. */
} cmddef;

/* Populate the table with code generated from Redis JSON files. */
static cmddef redis_commands[] = {
#define COMMAND(_type, _name, _subname, _firstkey, _arity)                     \
    {CMD_REQ_REDIS_##_type, _name, _subname, _firstkey, _arity},
#include "cmddef.h"
#undef COMMAND
};

/* Looks up a command or subcommand in the command table. Arg0 and arg1 are used
 * to lookup the command. The function returns CMD_UNKNOWN on failure. On
 * success, the command type is returned and *firstkey and *arity are
 * populated. */
cmd_type_t redis_lookup_cmd(const char *arg0, uint32_t arg0_len,
                            const char *arg1, uint32_t arg1_len, int *firstkey,
                            int *arity) {
    int num_commands = sizeof(redis_commands) / sizeof(cmddef);
    for (int i = 0; i < num_commands; i++) {
        cmddef *c = &redis_commands[i];

        if (strncasecmp(c->name, arg0, arg0_len))
            continue; /* Command mismatch. */

        if (c->subname != NULL) {
            if (arg1 == NULL || strncasecmp(c->subname, arg1, arg1_len))
                continue; /* Subcommand mismatch. */
        }

        /* Found it. */
        *firstkey = c->firstkey;
        *arity = c->arity;
        return c->type;
    }
    return CMD_UNKNOWN;
}

/*
 * Return true, if the redis command is a vector command accepting one or
 * more keys, otherwise return false
 * Format: command key [ key ... ]
 */
static int redis_argx(struct cmd *r) {
    switch (r->type) {
    case CMD_REQ_REDIS_EXISTS:
    case CMD_REQ_REDIS_MGET:
    case CMD_REQ_REDIS_DEL:
        return 1;

    default:
        break;
    }

    return 0;
}

/*
 * Return true, if the redis command is a vector command accepting one or
 * more key-value pairs, otherwise return false
 * Format: command key value [ key value ... ]
 */
static int redis_argkvx(struct cmd *r) {
    switch (r->type) {
    case CMD_REQ_REDIS_MSET:
        return 1;

    default:
        break;
    }

    return 0;
}

/*
 * Return true, if the redis command has the same syntax as EVAL. These commands
 * have a special format with exactly 2 arguments, followed by one or more keys,
 * followed by zero or more arguments (the documentation online seems to suggest
 * that at least one argument is required, but that shouldn't be the case).
 */
static int redis_argeval(struct cmd *r) {
    switch (r->type) {
    case CMD_REQ_REDIS_EVAL:
    case CMD_REQ_REDIS_EVAL_RO:
    case CMD_REQ_REDIS_EVALSHA:
    case CMD_REQ_REDIS_EVALSHA_RO:
    case CMD_REQ_REDIS_FCALL:
    case CMD_REQ_REDIS_FCALL_RO:
        return 1;

    default:
        break;
    }

    return 0;
}

/* Parses a bulk string starting at 'p' and ending somewhere before 'end'.
 * Returns the remaining of the input after consuming the bulk string. The
 * parsed string and its length are returned by reference. On parse error, NULL
 * is returned. */
char *redis_parse_bulk(char *p, char *end, char **str, uint32_t *len) {
    uint32_t length = 0;
    if (p >= end || *p++ != '$')
        return NULL;
    while (p < end && *p >= '0' && *p <= '9') {
        length = length * 10 + (uint32_t)(*p++ - '0');
    }
    if (p >= end || *p++ != CR)
        return NULL;
    if (p >= end || *p++ != LF)
        return NULL;
    *str = p;
    *len = length;
    p += length;
    if (p >= end || *p++ != CR)
        return NULL;
    if (p >= end || *p++ != LF)
        return NULL;
    return p;
}

/*
 * Reference: http://redis.io/topics/protocol
 *
 * Redis >= 1.2 uses the unified protocol to send requests to the Redis
 * server. In the unified protocol all the arguments sent to the server
 * are binary safe and every request has the following general form:
 *
 *   *<number of arguments> CR LF
 *   $<number of bytes of argument 1> CR LF
 *   <argument data> CR LF
 *   ...
 *   $<number of bytes of argument N> CR LF
 *   <argument data> CR LF
 *
 * Before the unified request protocol, redis protocol for requests supported
 * the following commands
 * 1). Inline commands: simple commands where arguments are just space
 *     separated strings. No binary safeness is possible.
 * 2). Bulk commands: bulk commands are exactly like inline commands, but
 *     the last argument is handled in a special way in order to allow for
 *     a binary-safe last argument.
 *
 * only supports the Redis unified protocol for requests.
 */
void redis_parse_cmd(struct cmd *r) {
    ASSERT(r->cmd != NULL && r->clen > 0);
    char *p = r->cmd;
    char *end = r->cmd + r->clen;
    uint32_t rnarg = 0;              /* Number of args including cmd name */
    char *arg0, *arg1 = NULL;        /* The first two args */
    uint32_t arg0_len, arg1_len = 0; /* The first two args' lengths */
    int firstkey;                    /* Position of the first key */
    int arity;                       /* Arity of the command */

    /* A command line is multi-bulk. */
    if (*p++ != '*')
        goto error;

    /* Parse multi-bulk size (rnarg). */
    while (p < end && *p >= '0' && *p <= '9') {
        rnarg = rnarg * 10 + (uint32_t)(*p++ - '0');
    }
    if (p == end || *p++ != CR)
        goto error;
    if (p == end || *p++ != LF)
        goto error;
    if (rnarg == 0)
        goto error;
    r->narg = rnarg;

    /* Parse the first two args. */
    p = redis_parse_bulk(p, end, &arg0, &arg0_len);
    if (p == NULL)
        goto error;
    if (rnarg > 1) {
        p = redis_parse_bulk(p, end, &arg1, &arg1_len);
        if (p == NULL)
            goto error;
    }

    /* Lookup command. */
    r->type =
        redis_lookup_cmd(arg0, arg0_len, arg1, arg1_len, &firstkey, &arity);
    if (r->type == CMD_UNKNOWN)
        goto error; /* Command not found. */
    if (arity >= 0 && (int)rnarg != arity)
        goto error; /* Exact arity check. */
    if (arity < 0 && (int)rnarg < -arity)
        goto error; /* Minimum arity check. */
    if (firstkey == 0)
        goto done; /* Command takes no keys. */

    /* Handle commands where firstkey depends on special logic. */
    if (firstkey < 0) {
        if (redis_argeval(r)) {
            /* Syntax: EVAL script numkeys [ key ... ] [ arg ... ] */
            char *numkeys;
            uint32_t numkeys_len;
            p = redis_parse_bulk(p, end, &numkeys, &numkeys_len);
            if (p == NULL)
                goto error;
            if (!strncmp("0", numkeys, numkeys_len)) {
                /* Zero keys in this command line. */
                goto done;
            } else {
                /* The first key is arg3. */
                char *key;
                uint32_t keylen;
                p = redis_parse_bulk(p, end, &key, &keylen);
                if (p == NULL)
                    goto error;
                struct keypos *kpos = hiarray_push(r->keys);
                if (kpos == NULL)
                    goto oom;
                kpos->start = key;
                kpos->end = key + keylen;
                goto done;
            }
        } else if (r->type == CMD_REQ_REDIS_MIGRATE) {
            /* MIGRATE host port <key | ""> destination-db timeout [COPY]
             * [REPLACE] [[AUTH password] | [AUTH2 username password]] [KEYS key
             * [key ...]]
             *
             * Not implemented. */
            goto error;
        } else {
            /* Not reached, only if Redis adds more commands. */
            goto error;
        }
    }

    /* Find first key arg. */
    char *key = arg1;
    uint32_t keylen = arg1_len;
    for (int i = 1; i < firstkey; i++) {
        p = redis_parse_bulk(p, end, &key, &keylen);
        if (p == NULL)
            goto error;
    }
    if (key == NULL)
        goto error; /* No key provided. */
    struct keypos *kpos = hiarray_push(r->keys);
    if (kpos == NULL)
        goto oom;
    kpos->start = key;
    kpos->end = key + keylen;

    /* Special commands where we want all keys (not only the first key). */
    if (redis_argx(r) || redis_argkvx(r)) {
        /* argx:   MGET key [ key ... ] */
        /* argkvx: MSET key value [ key value ... ] */
        if (redis_argkvx(r) && rnarg % 2 == 0)
            goto error;
        for (uint32_t i = 2; i < rnarg; i++) {
            p = redis_parse_bulk(p, end, &key, &keylen);
            if (p == NULL)
                goto error;
            if (redis_argkvx(r) && i % 2 == 0)
                continue; /* not a key */
            struct keypos *kpos = hiarray_push(r->keys);
            if (kpos == NULL)
                goto oom;
            kpos->start = key;
            kpos->end = key + keylen;
        }
    }

done:
    ASSERT(r->type > CMD_UNKNOWN && r->type < CMD_SENTINEL);
    r->result = CMD_PARSE_OK;
    return;

error:
    r->result = CMD_PARSE_ERROR;
    errno = EINVAL;
    if (r->errstr == NULL) {
        r->errstr = hi_malloc(100 * sizeof(*r->errstr));
        if (r->errstr == NULL) {
            goto oom;
        }
    }

    int len =
        _scnprintf(r->errstr, 100,
                   "Parse command error. Cmd type: %d, break position: %d.",
                   r->type, (int)(p - r->cmd));
    r->errstr[len] = '\0';
    return;

oom:
    r->result = CMD_PARSE_ENOMEM;
}

struct cmd *command_get() {
    struct cmd *command;
    command = hi_malloc(sizeof(struct cmd));
    if (command == NULL) {
        return NULL;
    }

    command->id = ++cmd_id;
    command->result = CMD_PARSE_OK;
    command->errstr = NULL;
    command->type = CMD_UNKNOWN;
    command->cmd = NULL;
    command->clen = 0;
    command->keys = NULL;
    command->narg = 0;
    command->quit = 0;
    command->noforward = 0;
    command->slot_num = -1;
    command->frag_seq = NULL;
    command->reply = NULL;
    command->sub_commands = NULL;
    command->node_addr = NULL;

    command->keys = hiarray_create(1, sizeof(struct keypos));
    if (command->keys == NULL) {
        hi_free(command);
        return NULL;
    }

    return command;
}

void command_destroy(struct cmd *command) {
    if (command == NULL) {
        return;
    }

    if (command->cmd != NULL) {
        hi_free(command->cmd);
        command->cmd = NULL;
    }

    if (command->errstr != NULL) {
        hi_free(command->errstr);
        command->errstr = NULL;
    }

    if (command->keys != NULL) {
        command->keys->nelem = 0;
        hiarray_destroy(command->keys);
        command->keys = NULL;
    }

    if (command->frag_seq != NULL) {
        hi_free(command->frag_seq);
        command->frag_seq = NULL;
    }

    freeReplyObject(command->reply);

    if (command->sub_commands != NULL) {
        listRelease(command->sub_commands);
    }

    if (command->node_addr != NULL) {
        sdsfree(command->node_addr);
        command->node_addr = NULL;
    }

    hi_free(command);
}

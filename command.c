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

typedef enum {
    KEYPOS_NONE,
    KEYPOS_UNKNOWN,
    KEYPOS_INDEX,
    KEYPOS_KEYNUM
} cmd_keypos;

typedef struct {
    cmd_type_t type;           /* A constant identifying the command. */
    const char *name;          /* Command name */
    const char *subname;       /* Subcommand name or NULL */
    cmd_keypos firstkeymethod; /* First key none, unknown, pos or keynum */
    int8_t firstkeypos;        /* Position of first key or the  arg */
    int8_t arity;              /* Arity, neg number means min num args */
} cmddef;

/* Populate the table with code in cmddef.h generated from Redis JSON files. */
static cmddef redis_commands[] = {
#define COMMAND(_type, _name, _subname, _arity, _keymethod, _keypos)           \
    {.type = CMD_REQ_REDIS_##_type,                                            \
     .name = _name,                                                            \
     .subname = _subname,                                                      \
     .firstkeymethod = KEYPOS_##_keymethod,                                    \
     .firstkeypos = _keypos,                                                   \
     .arity = _arity},
#include "cmddef.h"
#undef COMMAND
};

/* Looks up a command or subcommand in the command table. Arg0 and arg1 are used
 * to lookup the command. The function returns CMD_UNKNOWN on failure. On
 * success, the command type is returned and *firstkey and *arity are
 * populated. */
cmddef *redis_lookup_cmd(const char *arg0, uint32_t arg0_len, const char *arg1,
                         uint32_t arg1_len) {
    int num_commands = sizeof(redis_commands) / sizeof(cmddef);
    /* Find the command using binary search. */
    int left = 0, right = num_commands - 1;
    while (left <= right) {
        int i = (left + right) / 2;
        cmddef *c = &redis_commands[i];

        int cmp = strncasecmp(c->name, arg0, arg0_len);
        if (cmp == 0 && strlen(c->name) > arg0_len)
            cmp = 1; /* "HGETALL" vs "HGET" */

        /* If command name matches, compare subcommand if any */
        if (cmp == 0 && c->subname != NULL) {
            if (arg1 == NULL) {
                /* Command has subcommands, but none given. */
                return NULL;
            } else {
                cmp = strncasecmp(c->subname, arg1, arg1_len);
                if (cmp == 0 && strlen(c->subname) > arg1_len)
                    cmp = 1;
            }
        }

        if (cmp < 0) {
            left = i + 1;
        } else if (cmp > 0) {
            right = i - 1;
        } else {
            /* Found it. */
            return c;
        }
    }
    return NULL;
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

/* Parses a bulk string starting at 'p' and ending somewhere before 'end'.
 * Returns the remaining of the input after consuming the bulk string. The
 * pointers *str and *len are pointed to the parsed string and its length. On
 * parse error, NULL is returned. */
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
    if (str)
        *str = p;
    if (len)
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
    uint32_t rnarg = 0;                  /* Number of args including cmd name */
    int argidx = -1;                     /* Index of last parsed arg */
    char *arg;                           /* Last parsed arg */
    uint32_t arglen;                     /* Length of arg */
    char *arg0 = NULL, *arg1 = NULL;     /* The first two args */
    uint32_t arg0_len = 0, arg1_len = 0; /* Lengths of arg0 and arg1 */
    cmddef *info = NULL;                 /* Command info, when found */

    /* Check that the command line is multi-bulk. */
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
    if ((p = redis_parse_bulk(p, end, &arg0, &arg0_len)) == NULL)
        goto error;
    argidx++;
    if (rnarg > 1) {
        if ((p = redis_parse_bulk(p, end, &arg1, &arg1_len)) == NULL)
            goto error;
        argidx++;
    }

    /* Lookup command. */
    if ((info = redis_lookup_cmd(arg0, arg0_len, arg1, arg1_len)) == NULL)
        goto error; /* Command not found. */
    r->type = info->type;

    /* Arity check (negative arity means minimum num args) */
    if ((info->arity >= 0 && (int)rnarg != info->arity) ||
        (info->arity < 0 && (int)rnarg < -info->arity)) {
        goto error;
    }
    if (info->firstkeymethod == KEYPOS_NONE)
        goto done; /* Command takes no keys. */
    if (arg1 == NULL)
        goto error; /* Command takes keys, but no args given. Quick abort. */

    /* Below we assume arg1 != NULL, */

    /* Handle commands where firstkey depends on special logic. */
    if (info->firstkeymethod == KEYPOS_UNKNOWN) {
        /* Keyword-based first key position */
        const char *keyword;
        int startfrom;
        if (r->type == CMD_REQ_REDIS_XREAD) {
            keyword = "STREAMS";
            startfrom = 1;
        } else if (r->type == CMD_REQ_REDIS_XREADGROUP) {
            keyword = "STREAMS";
            startfrom = 4;
        } else {
            /* Not reached, but can be reached if Redis adds more commands. */
            goto error;
        }

        /* Skip forward to the 'startfrom' arg index, then search for the keyword. */
        arg = arg1;
        arglen = arg1_len;
        while (argidx < (int)rnarg - 1) {
            if ((p = redis_parse_bulk(p, end, &arg, &arglen)) == NULL)
                goto error; /* Keyword not provided, thus no keys. */
            if (argidx++ < startfrom)
                continue; /* Keyword can't appear in a position before 'startfrom' */
            if (!strncasecmp(keyword, arg, arglen)) {
                /* Keyword found. Now the first key is the next arg. */
                if ((p = redis_parse_bulk(p, end, &arg, &arglen)) == NULL)
                    goto error;
                struct keypos *kpos = hiarray_push(r->keys);
                if (kpos == NULL)
                    goto oom;
                kpos->start = arg;
                kpos->end = arg + arglen;
                goto done;
            }
        }

        /* Keyword not provided. */
        goto error;
    }

    /* Find first key arg. */
    arg = arg1;
    arglen = arg1_len;
    for (; argidx < info->firstkeypos; argidx++) {
        if ((p = redis_parse_bulk(p, end, &arg, &arglen)) == NULL)
            goto error;
    }

    if (info->firstkeymethod == KEYPOS_KEYNUM) {
        /* The arg specifies the number of keys and the first key is the next
         * arg. Example:
         *
         * EVAL script numkeys [key [key ...]] [arg [arg ...]] */
        if (!strncmp("0", arg, arglen))
            goto done; /* No args. */
        /* One or more args. The first key is the arg after the 'numkeys' arg. */
        if ((p = redis_parse_bulk(p, end, &arg, &arglen)) == NULL)
            goto error;
        argidx++;
    }

    /* Now arg is the first key and arglen is its length. */

    if (info->type == CMD_REQ_REDIS_MIGRATE && arglen == 0 &&
        info->firstkeymethod == KEYPOS_INDEX && info->firstkeypos == 3) {
        /* MIGRATE host port <key | ""> destination-db timeout [COPY] [REPLACE]
         * [[AUTH password] | [AUTH2 username password]] [KEYS key [key ...]]
         *
         * The key spec points out arg3 as the first key, but if it's an empty
         * string, we would need to search for the KEYS keyword arg backwards
         * from the end of the command line. This is not implemented. */
        goto error;
    }

    struct keypos *kpos = hiarray_push(r->keys);
    if (kpos == NULL)
        goto oom;
    kpos->start = arg;
    kpos->end = arg + arglen;

    /* Special commands where we want all keys (not only the first key). */
    if (redis_argx(r) || redis_argkvx(r)) {
        /* argx:   MGET key [ key ... ] */
        /* argkvx: MSET key value [ key value ... ] */
        if (redis_argkvx(r) && rnarg % 2 == 0)
            goto error;
        for (uint32_t i = 2; i < rnarg; i++) {
            if ((p = redis_parse_bulk(p, end, &arg, &arglen)) == NULL)
                goto error;
            if (redis_argkvx(r) && i % 2 == 0)
                continue; /* not a key */
            struct keypos *kpos = hiarray_push(r->keys);
            if (kpos == NULL)
                goto oom;
            kpos->start = arg;
            kpos->end = arg + arglen;
        }
    }

done:
    ASSERT(r->type > CMD_UNKNOWN && r->type < CMD_SENTINEL);
    r->result = CMD_PARSE_OK;
    return;

error:
    r->result = CMD_PARSE_ERROR;
    errno = EINVAL;
    size_t errmaxlen = 100; /* Enough for the error messages below. */
    if (r->errstr == NULL) {
        r->errstr = hi_malloc(errmaxlen);
        if (r->errstr == NULL) {
            goto oom;
        }
    }

    if (info != NULL && info->subname != NULL)
        snprintf(r->errstr, errmaxlen, "Failed to find keys of command %s %s",
                 info->name, info->subname);
    else if (info != NULL)
        snprintf(r->errstr, errmaxlen, "Failed to find keys of command %s",
                 info->name);
    else if (r->type == CMD_UNKNOWN && arg0 != NULL && arg1 != NULL)
        snprintf(r->errstr, errmaxlen, "Unknown command %.*s %.*s", arg0_len,
                 arg0, arg1_len, arg1);
    else if (r->type == CMD_UNKNOWN && arg0 != NULL)
        snprintf(r->errstr, errmaxlen, "Unknown command %.*s", arg0_len, arg0);
    else
        snprintf(r->errstr, errmaxlen, "Command parse error");
    return;

oom:
    r->result = CMD_PARSE_ENOMEM;
}

struct cmd *command_get(void) {
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

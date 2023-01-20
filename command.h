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

#ifndef __COMMAND_H_
#define __COMMAND_H_

#include <stdint.h>

#include "adlist.h"
#include <hiredis/hiredis.h>

typedef enum cmd_parse_result {
    CMD_PARSE_OK,     /* parsing ok */
    CMD_PARSE_ENOMEM, /* out of memory */
    CMD_PARSE_ERROR,  /* parsing error */
    CMD_PARSE_REPAIR, /* more to parse -> repair parsed & unparsed data */
    CMD_PARSE_AGAIN,  /* incomplete -> parse again */
} cmd_parse_result_t;

typedef enum cmd_type {
    CMD_UNKNOWN,
/* Request commands */
#define COMMAND(_type, _name, _subname, _arity, _keymethod, _keypos)           \
    CMD_REQ_REDIS_##_type,
#include "cmddef.h"
#undef COMMAND
    /* Response types */
    CMD_RSP_REDIS_STATUS, /* simple string */
    CMD_RSP_REDIS_ERROR,
    CMD_RSP_REDIS_INTEGER,
    CMD_RSP_REDIS_BULK,
    CMD_RSP_REDIS_MULTIBULK,
    CMD_SENTINEL
} cmd_type_t;

struct keypos {
    char *start;         /* key start pos */
    char *end;           /* key end pos */
    uint32_t remain_len; /* remain length after keypos->end for more key-value
                            pairs in command, like mset */
};

struct cmd {

    uint64_t id; /* command id */

    cmd_parse_result_t result; /* command parsing result */
    char *errstr;              /* error info when the command parse failed */

    cmd_type_t type; /* command type */

    char *cmd;
    uint32_t clen; /* command length */

    struct hiarray *keys; /* array of keypos, for req */

    uint32_t narg; /* # arguments (redis) */

    unsigned quit : 1;      /* quit request? */
    unsigned noforward : 1; /* not need forward (example: ping) */

    /* Command destination */
    int slot_num;    /* Command should be sent to slot.
                      * Set to -1 if command is sent to a given node,
                      * or if a slot can not be found or calculated,
                      * or if its a multi-key command cross different
                      * nodes (cross slot) */
    char *node_addr; /* Command sent to this node address */

    struct cmd *
        *frag_seq; /* sequence of fragment command, map from keys to fragments*/

    redisReply *reply;

    hilist *sub_commands; /* just for pipeline and multi-key commands */
};

void redis_parse_cmd(struct cmd *r);

struct cmd *command_get(void);
void command_destroy(struct cmd *command);

#endif

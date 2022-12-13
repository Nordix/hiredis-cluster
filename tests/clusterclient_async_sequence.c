/*
 * This program connects to a Redis node and then reads commands from stdin, such
 * as "SET foo bar", one per line and prints the results to stdout.
 *
 * The behaviour is similar to that of clusterclient_async.c, but it sends the
 * next command after receiving a reply from the previous command.
 * It is still possible to send multiple commands at once like in
 * clusterclient_async.c using following action commands:
 *
 * !async  - Send multiple commands and then wait for their responses.
 *           Will send all following commands until EOF or the command `!sync`
 *
 * !sync   - Send a single command and wait for its response before sending next
 *           command. This is the default behaviour.
 *
 * !resend - Resend a failed command from its reply callback.
 *           Will resend all following failed commands until EOF.
 *
 * An example input of first sending 2 commands and waiting for their responses,
 * before sending a single command and waiting for its response:
 *
 * !async
 * SET dual-1 command
 * SET dual-2 command
 * !sync
 * SET single command
 *
 */

#include "adapters/libevent.h"
#include "hircluster.h"
#include "test_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMD_SIZE 256
#define HISTORY_DEPTH 16

char cmd_history[HISTORY_DEPTH][CMD_SIZE];

int num_running = 0;
int resend_failed_cmd = 0;

void sendNextCommand(int, short, void *);

void replyCallback(redisClusterAsyncContext *acc, void *r, void *privdata) {
    redisReply *reply = (redisReply *)r;
    intptr_t cmd_id = (intptr_t)privdata; /* Id to corresponding cmd */

    if (reply == NULL) {
        if (acc->err) {
            printf("error: %s\n", acc->errstr);
        } else {
            printf("unknown error\n");
        }

        if (resend_failed_cmd) {
            printf("resend '%s'\n", cmd_history[cmd_id]);
            if (redisClusterAsyncCommand(acc, replyCallback, (void *)cmd_id,
                                         cmd_history[cmd_id]) != REDIS_OK)
                printf("send error\n");
            return;
        }
    } else {
        printf("%s\n", reply->str);
    }

    if (--num_running == 0) {
        /* Schedule a read from stdin and send next command */
        event_base_once(acc->adapter, -1, EV_TIMEOUT, sendNextCommand, acc,
                        NULL);
    }
}

void sendNextCommand(int fd, short kind, void *arg) {
    UNUSED(fd);
    UNUSED(kind);
    redisClusterAsyncContext *acc = arg;
    int async = 0;

    char cmd[CMD_SIZE];
    while (fgets(cmd, CMD_SIZE, stdin)) {
        size_t len = strlen(cmd);
        if (cmd[len - 1] == '\n') /* Chop trailing line break */
            cmd[len - 1] = '\0';

        if (cmd[0] == '\0') /* Skip empty lines */
            continue;
        if (cmd[0] == '#') /* Skip comments */
            continue;
        if (cmd[0] == '!') {
            if (strcmp(cmd, "!async") == 0) /* Enable async send */
                async = 1;
            if (strcmp(cmd, "!sync") == 0) { /* Disable async send */
                if (async)
                    return; /* We are done sending commands */
            }
            if (strcmp(cmd, "!resend") == 0) /* Enable resend of failed cmd */
                resend_failed_cmd = 1;
            continue; /* Skip line */
        }

        /* Copy command string to history buffer */
        assert(num_running < HISTORY_DEPTH);
        strcpy(cmd_history[num_running], cmd);

        int status = redisClusterAsyncCommand(
            acc, replyCallback, (void *)((intptr_t)num_running), cmd);
        ASSERT_MSG(status == REDIS_OK, acc->errstr);
        num_running++;

        if (async)
            continue; /* Send next command as well */

        return;
    }

    /* Disconnect if nothing is left to read from stdin */
    redisClusterAsyncDisconnect(acc);
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        fprintf(stderr, "Usage: %s HOST:PORT\n", argv[0]);
        exit(1);
    }
    const char *initnode = argv[1];
    struct timeval timeout = {0, 500000};

    redisClusterAsyncContext *acc = redisClusterAsyncContextInit();
    assert(acc);
    redisClusterSetOptionAddNodes(acc->cc, initnode);
    redisClusterSetOptionRouteUseSlots(acc->cc);
    redisClusterSetOptionTimeout(acc->cc, timeout);
    redisClusterSetOptionConnectTimeout(acc->cc, timeout);
    redisClusterSetOptionMaxRetry(acc->cc, 1);

    if (redisClusterConnect2(acc->cc) != REDIS_OK) {
        printf("Connect error: %s\n", acc->cc->errstr);
        exit(-1);
    }

    struct event_base *base = event_base_new();
    int status = redisClusterLibeventAttach(acc, base);
    assert(status == REDIS_OK);

    /* Schedule a read from stdin and send next command */
    event_base_once(acc->adapter, -1, EV_TIMEOUT, sendNextCommand, acc, NULL);

    event_base_dispatch(base);

    redisClusterAsyncFree(acc);
    event_base_free(base);
    return 0;
}

/*
 * This program connects to a cluster and then reads commands from stdin, such
 * as "SET foo bar", one per line and prints the results to stdout.
 */

#include "hircluster.h"
#include "win32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc <= 1) {
        fprintf(stderr, "Usage: clusterclient HOST:PORT\n");
        exit(1);
    }
    const char *initnode = argv[1];

    struct timeval timeout = {1, 500000}; // 1.5s

    redisClusterContext *cc = redisClusterContextInit();
    redisClusterSetOptionAddNodes(cc, initnode);
    redisClusterSetOptionConnectTimeout(cc, timeout);
    redisClusterSetOptionRouteUseSlots(cc);
    redisClusterSetOptionMaxRetry(cc, 2);

    if (redisClusterConnect2(cc) != REDIS_OK) {
        fprintf(stderr, "Connect error: %s\n", cc->errstr);
        exit(100);
    }

    char cmd[256];
    while (fgets(cmd, 256, stdin)) {
        size_t len = strlen(cmd);
        if (cmd[len - 1] == '\n') // Chop trailing line break
            cmd[len - 1] = '\0';

        if (cmd[0] == '\0') /* Skip empty lines */
            continue;
        if (cmd[0] == '#') /* Skip comments */
            continue;

        redisReply *reply = (redisReply *)redisClusterCommand(cc, cmd);
        if (cc->err) {
            printf("error: %s\n", cc->errstr);
        } else {
            printf("%s\n", reply->str);
        }
        freeReplyObject(reply);
    }

    redisClusterFree(cc);
    return 0;
}

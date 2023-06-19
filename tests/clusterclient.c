/*
 * This program connects to a cluster and then reads commands from stdin, such
 * as "SET foo bar", one per line and prints the results to stdout.
 */

#include "hircluster.h"
#include "win32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void eventCallback(const redisClusterContext *cc, int event) {
    (void)cc;
    char *e = event == HIRCLUSTER_EVENT_SLOTMAP_UPDATED ? "slotmap-updated" :
                                                          "unknown";
    printf("Event: %s\n", e);
}

int main(int argc, char **argv) {
    int show_events = 0;

    int argindex;
    for (argindex = 1; argindex < argc && argv[argindex][0] == '-';
         argindex++) {
        if (strcmp(argv[argindex], "--events") == 0) {
            show_events = 1;
        } else {
            fprintf(stderr, "Unknown argument: '%s'\n", argv[argindex]);
        }
    }

    if (argindex >= argc) {
        fprintf(stderr, "Usage: clusterclient [--events] HOST:PORT\n");
        exit(1);
    }
    const char *initnode = argv[argindex];

    struct timeval timeout = {1, 500000}; // 1.5s

    redisClusterContext *cc = redisClusterContextInit();
    redisClusterSetOptionAddNodes(cc, initnode);
    redisClusterSetOptionConnectTimeout(cc, timeout);
    redisClusterSetOptionRouteUseSlots(cc);
    if (show_events) {
        redisClusterSetEventCallback(cc, eventCallback);
    }

    if (redisClusterConnect2(cc) != REDIS_OK) {
        fprintf(stderr, "Connect error: %s\n", cc->errstr);
        exit(100);
    }

    char command[256];
    while (fgets(command, 256, stdin)) {
        size_t len = strlen(command);
        if (command[len - 1] == '\n') // Chop trailing line break
            command[len - 1] = '\0';
        redisReply *reply = (redisReply *)redisClusterCommand(cc, command);
        if (cc->err) {
            fprintf(stderr, "redisClusterCommand error: %s\n", cc->errstr);
            exit(101);
        }
        printf("%s\n", reply->str);
        freeReplyObject(reply);
    }

    redisClusterFree(cc);
    return 0;
}

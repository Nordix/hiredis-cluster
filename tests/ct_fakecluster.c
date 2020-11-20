#include "fakecluster.h"
#include "hircluster.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_CLUSTER_FIRST_PORT 4455
#define FAKE_CLUSTER_NUM_OF_NODES 6
#define FAKE_CLUSTER_INITNODE "127.0.0.1:4455"

#define ASSERT_MSG(_x, _msg)                                                   \
    if (!(_x)) {                                                               \
        fprintf(stderr, "ERROR: %s\n", _msg);                                  \
        assert(_x);                                                            \
    }

// Test of a cluster that suddenly is inaccessible
void test_cluster_stops() {
    int res;

    // Start a cluster
    fakecluster_start(FAKE_CLUSTER_FIRST_PORT, FAKE_CLUSTER_NUM_OF_NODES);

    // Setup client
    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);
    res = redisClusterSetOptionAddNodes(cc, FAKE_CLUSTER_INITNODE);
    ASSERT_MSG(res == REDIS_OK, cc->errstr);

    struct timeval timeout = {0, 500000};
    res = redisClusterSetOptionConnectTimeout(cc, timeout);
    ASSERT_MSG(res == REDIS_OK, cc->errstr);
    res = redisClusterSetOptionTimeout(cc, timeout);
    ASSERT_MSG(res == REDIS_OK, cc->errstr);
    res = redisClusterSetOptionMaxRedirect(cc, 3);
    ASSERT_MSG(res == REDIS_OK, cc->errstr);

    // Perform initial connect to get cluster info
    res = redisClusterConnect2(cc);
    ASSERT_MSG(res == REDIS_OK, cc->errstr);

    // Request towards running cluster
    redisReply *reply;
    reply = (redisReply *)redisClusterCommand(cc, "SET key HelloWorld");
    assert(reply);
    assert(strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);

    // Stop cluster
    fakecluster_stop();

    // Request towards stopped cluster
    reply = (redisReply *)redisClusterCommand(cc, "SET key HelloWorld");
    assert(reply == NULL);

    redisClusterFree(cc);
}

int main() {

    test_cluster_stops();

    return 0;
}

#include "hircluster.h"
#include "test_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

void test_exists(redisClusterContext *cc) {
    redisReply *reply;
    reply = (redisReply *)redisClusterCommand(cc, "SET key1 Hello");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "EXISTS key1");
    CHECK_REPLY_INT(cc, reply, 1);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "EXISTS nosuchkey");
    CHECK_REPLY_INT(cc, reply, 0);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "SET key2 World");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "EXISTS key1 key2 nosuchkey");
    CHECK_REPLY_INT(cc, reply, 2);
    freeReplyObject(reply);
}

void test_mset(redisClusterContext *cc) {
    redisReply *reply;
    reply = (redisReply *)redisClusterCommand(
        cc, "MSET key1 mset1 key2 mset2 key3 mset3");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "GET key1");
    CHECK_REPLY_STR(cc, reply, "mset1");
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "GET key2");
    CHECK_REPLY_STR(cc, reply, "mset2");
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "GET key3");
    CHECK_REPLY_STR(cc, reply, "mset3");
    freeReplyObject(reply);
}

void test_mget(redisClusterContext *cc) {
    redisReply *reply;
    reply = (redisReply *)redisClusterCommand(cc, "SET key1 mget1");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "SET key2 mget2");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "SET key3 mget3");
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    reply = (redisReply *)redisClusterCommand(cc, "MGET key1 key2 key3");
    CHECK_REPLY_ARRAY(cc, reply, 3);
    CHECK_REPLY_STR(cc, reply->element[0], "mget1");
    CHECK_REPLY_STR(cc, reply->element[1], "mget2");
    CHECK_REPLY_STR(cc, reply->element[2], "mget3");
    freeReplyObject(reply);
}

int main() {
    struct timeval timeout = {0, 500000};

    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);
    redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
    redisClusterSetOptionConnectTimeout(cc, timeout);

    int status;
    status = redisClusterConnect2(cc);
    ASSERT_MSG(status == REDIS_OK, cc->errstr);

    test_exists(cc);
    test_mset(cc);
    test_mget(cc);

    redisClusterFree(cc);
    return 0;
}

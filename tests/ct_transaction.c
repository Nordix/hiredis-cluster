#include "hircluster.h"
#include "test_utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

// Test of transactions in using the pipelined sync api
void test_pipelined_transaction() {
    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);

    int status;
    status = redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
    ASSERT_MSG(status == REDIS_OK, cc->errstr);

    status = redisClusterConnect2(cc);
    ASSERT_MSG(status == REDIS_OK, cc->errstr);

    status = redisClusterAppendCommand(cc, "MULTI");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "SET bar five");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "SET foo five"); // New slot..
    ASSERT_MSG(status == REDIS_ERR, cc->errstr);            // ..gives REDIS_ERR
    status = redisClusterAppendCommand(cc, "GET bar");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "EXEC");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);

    redisReply *reply;
    redisClusterGetReply(cc, (void *)&reply); // reply for: MULTI
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: SET
    CHECK_REPLY_QUEUED(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: GET
    CHECK_REPLY_QUEUED(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: EXEC
    CHECK_REPLY_ARRAY(cc, reply, 2);
    CHECK_REPLY_OK(cc, reply->element[0]);
    CHECK_REPLY_STR(cc, reply->element[1], "five");
    freeReplyObject(reply);

    redisClusterFree(cc);
}

// Test of discarding a transaction
void test_discarded_pipelined_transaction() {
    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);

    int status;
    status = redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
    ASSERT_MSG(status == REDIS_OK, cc->errstr);

    status = redisClusterConnect2(cc);
    ASSERT_MSG(status == REDIS_OK, cc->errstr);

    status = redisClusterAppendCommand(cc, "SET foo 55");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "MULTI");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "INCR foo");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "DISCARD");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "GET foo");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "MULTI");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "INCR foo");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "EXEC");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);
    status = redisClusterAppendCommand(cc, "GET foo");
    ASSERT_MSG(status == REDIS_OK, cc->errstr);

    redisReply *reply;
    redisClusterGetReply(cc, (void *)&reply); // reply for: SET
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: MULTI
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: INCR
    CHECK_REPLY_QUEUED(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: DISCARD
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: GET
    CHECK_REPLY_STR(cc, reply, "55");
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: MULTI
    CHECK_REPLY_OK(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: INCR
    CHECK_REPLY_QUEUED(cc, reply);
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: EXEC
    CHECK_REPLY_ARRAY(cc, reply, 1);
    CHECK_REPLY_INT(cc, reply->element[0], 56); // INCR
    freeReplyObject(reply);

    redisClusterGetReply(cc, (void *)&reply); // reply for: GET
    CHECK_REPLY_STR(cc, reply, "56");
    freeReplyObject(reply);

    redisClusterFree(cc);
}

int main() {

    test_pipelined_transaction();
    test_discarded_pipelined_transaction();

    return 0;
}

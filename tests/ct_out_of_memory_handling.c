/* Testcases that simulates allocation failures during hiredis-cluster API calls
 * which verifies the handling of out of memory scenarios (OOM).
 *
 * These testcases overrides the default allocators by injecting own functions
 * which can be configured to fail after a given number of successful allocations.
 * A testcase can use a prepare function like `prepare_allocation_test()` to
 * set the number of successful allocations that follows. The allocator will then
 * count the number of calls before it start to return OOM failures, like
 * malloc() returning NULL.
 *
 * Tests will call a hiredis-cluster API-function while iterating on a number,
 * the number of successful allocations during the call before it hits an OOM.
 * The result and the error code is then checked to show "Out of memory".
 * As a last step the correct number of allocations is prepared to get a
 * successful API-function call.
 *
 * Tip:
 * When this testcase fails after code changes in the library, run the testcase
 * in `gdb` to find which API call that failed, and in which iteration.
 * - Go to the correct stack frame to find which API that triggered a failure.
 * - Use the gdb command `print i` to find which iteration.
 * - Investigate if a failure or a success is expected after the code change.
 * - Set correct `i` in for-loop and the `prepare_allocation_test()` for the test.
 *   Correct `i` can be hard to know, finding the correct number might require trial
 *   and error of running with increased/decreased `i` until the edge is found.
 */
#include "adapters/libevent.h"
#include "hircluster.h"
#include "test_utils.h"
#include "win32.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

int successfulAllocations = 0;
bool assertWhenAllocFail = false; // Enable for troubleshooting

// A configurable OOM failing malloc()
static void *hi_malloc_fail(size_t size) {
    if (successfulAllocations > 0) {
        --successfulAllocations;
        return malloc(size);
    }
    assert(assertWhenAllocFail == false);
    return NULL;
}

// A  configurable OOM failing calloc()
static void *hi_calloc_fail(size_t nmemb, size_t size) {
    if (successfulAllocations > 0) {
        --successfulAllocations;
        return calloc(nmemb, size);
    }
    assert(assertWhenAllocFail == false);
    return NULL;
}

// A  configurable OOM failing realloc()
static void *hi_realloc_fail(void *ptr, size_t size) {
    if (successfulAllocations > 0) {
        --successfulAllocations;
        return realloc(ptr, size);
    }
    assert(assertWhenAllocFail == false);
    return NULL;
}

/* Prepare the test fixture.
 * Configures the allocator functions with the number of allocations
 * that will succeed before simulating an out of memory scenario.
 * Additionally it resets errors in the cluster context. */
void prepare_allocation_test(redisClusterContext *cc,
                             int _successfulAllocations) {
    successfulAllocations = _successfulAllocations;
    cc->err = 0;
    memset(cc->errstr, '\0', strlen(cc->errstr));
}

void prepare_allocation_test_async(redisClusterAsyncContext *acc,
                                   int _successfulAllocations) {
    successfulAllocations = _successfulAllocations;
    acc->err = 0;
    memset(acc->errstr, '\0', strlen(acc->errstr));
}

/* Helper */
redisClusterNode *getNodeByPort(redisClusterContext *cc, int port) {
    redisClusterNodeIterator ni;
    redisClusterInitNodeIterator(&ni, cc);
    redisClusterNode *node;
    while ((node = redisClusterNodeNext(&ni)) != NULL) {
        if (node->port == port)
            return node;
    }
    assert(0);
    return NULL;
}

/* Test of allocation handling in the blocking API */
void test_alloc_failure_handling(void) {
    int result;
    hiredisAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };
    // Override allocators
    hiredisSetAllocators(&ha);

    // Context init
    redisClusterContext *cc;
    {
        successfulAllocations = 0;
        cc = redisClusterContextInit();
        assert(cc == NULL);

        successfulAllocations = 1;
        cc = redisClusterContextInit();
        assert(cc);
    }

    // Add nodes
    {
        for (int i = 0; i < 9; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        prepare_allocation_test(cc, 9);
        result = redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
        assert(result == REDIS_OK);
    }

    // Set connect timeout
    {
        struct timeval timeout = {0, 500000};

        prepare_allocation_test(cc, 0);
        result = redisClusterSetOptionConnectTimeout(cc, timeout);
        assert(result == REDIS_ERR);
        ASSERT_STR_EQ(cc->errstr, "Out of memory");

        prepare_allocation_test(cc, 1);
        result = redisClusterSetOptionConnectTimeout(cc, timeout);
        assert(result == REDIS_OK);
    }

    // Set request timeout
    {
        struct timeval timeout = {0, 500000};

        prepare_allocation_test(cc, 0);
        result = redisClusterSetOptionTimeout(cc, timeout);
        assert(result == REDIS_ERR);
        ASSERT_STR_EQ(cc->errstr, "Out of memory");

        prepare_allocation_test(cc, 1);
        result = redisClusterSetOptionTimeout(cc, timeout);
        assert(result == REDIS_OK);
    }

    // Connect
    {
        for (int i = 0; i < 128; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterConnect2(cc);
            assert(result == REDIS_ERR);
        }

        prepare_allocation_test(cc, 128);
        result = redisClusterConnect2(cc);
        assert(result == REDIS_OK);
    }

    // Command
    {
        redisReply *reply;
        const char *cmd = "SET key value";

        for (int i = 0; i < 36; ++i) {
            prepare_allocation_test(cc, i);
            reply = (redisReply *)redisClusterCommand(cc, cmd);
            assert(reply == NULL);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        prepare_allocation_test(cc, 36);
        reply = (redisReply *)redisClusterCommand(cc, cmd);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Multi key command
    {
        redisReply *reply;
        const char *cmd = "MSET key1 v1 key2 v2 key3 v3";

        for (int i = 0; i < 77; ++i) {
            prepare_allocation_test(cc, i);
            reply = (redisReply *)redisClusterCommand(cc, cmd);
            assert(reply == NULL);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        // Multi-key commands
        prepare_allocation_test(cc, 77);
        reply = (redisReply *)redisClusterCommand(cc, cmd);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Command to node
    {
        redisReply *reply;
        const char *cmd = "SET key value";

        redisClusterNode *node = redisClusterGetNodeByKey(cc, "key");
        assert(node);

        // OOM failing commands
        for (int i = 0; i < 32; ++i) {
            prepare_allocation_test(cc, i);
            reply = redisClusterCommandToNode(cc, node, cmd);
            assert(reply == NULL);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        // Successful command
        prepare_allocation_test(cc, 32);
        reply = redisClusterCommandToNode(cc, node, cmd);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Append command
    {
        redisReply *reply;
        const char *cmd = "SET foo one";

        for (int i = 0; i < 37; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");

            redisClusterReset(cc);
        }

        for (int i = 0; i < 4; ++i) {
            // Appended command lost when receiving error from hiredis
            // during a GetReply, needs a new append for each test loop
            prepare_allocation_test(cc, 37);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_OK);

            prepare_allocation_test(cc, i);
            result = redisClusterGetReply(cc, (void *)&reply);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");

            redisClusterReset(cc);
        }

        prepare_allocation_test(cc, 37);
        result = redisClusterAppendCommand(cc, cmd);
        assert(result == REDIS_OK);

        prepare_allocation_test(cc, 4);
        result = redisClusterGetReply(cc, (void *)&reply);
        assert(result == REDIS_OK);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Append multi-key command
    {
        redisReply *reply;
        const char *cmd = "MSET key1 val1 key2 val2 key3 val3";

        for (int i = 0; i < 90; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");

            redisClusterReset(cc);
        }

        for (int i = 0; i < 12; ++i) {
            prepare_allocation_test(cc, 90);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_OK);

            prepare_allocation_test(cc, i);
            result = redisClusterGetReply(cc, (void *)&reply);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");

            redisClusterReset(cc);
        }

        prepare_allocation_test(cc, 90);
        result = redisClusterAppendCommand(cc, cmd);
        assert(result == REDIS_OK);

        prepare_allocation_test(cc, 12);
        result = redisClusterGetReply(cc, (void *)&reply);
        assert(result == REDIS_OK);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Append command to node
    {
        redisReply *reply;
        const char *cmd = "SET foo one";

        redisClusterNode *node = redisClusterGetNodeByKey(cc, "foo");
        assert(node);

        // OOM failing appends
        for (int i = 0; i < 37; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterAppendCommandToNode(cc, node, cmd);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");

            redisClusterReset(cc);
        }

        // OOM failing GetResults
        for (int i = 0; i < 4; ++i) {
            // First a successful append
            prepare_allocation_test(cc, 37);
            result = redisClusterAppendCommandToNode(cc, node, cmd);
            assert(result == REDIS_OK);

            prepare_allocation_test(cc, i);
            result = redisClusterGetReply(cc, (void *)&reply);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");

            redisClusterReset(cc);
        }

        // Successful append and GetReply
        prepare_allocation_test(cc, 37);
        result = redisClusterAppendCommandToNode(cc, node, cmd);
        assert(result == REDIS_OK);

        prepare_allocation_test(cc, 4);
        result = redisClusterGetReply(cc, (void *)&reply);
        assert(result == REDIS_OK);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Redirects
    {
        /* Skip OOM testing during the prepare steps by allowing a high number of
         * allocations. A specific number of allowed allocations will be used later
         * in the testcase when we run commands that results in redirects. */
        prepare_allocation_test(cc, 1000);

        /* Get the source information for the migration. */
        unsigned int slot = redisClusterGetSlotByKey("foo");
        redisClusterNode *srcNode = redisClusterGetNodeByKey(cc, "foo");
        int srcPort = srcNode->port;

        /* Get a destination node to migrate the slot to. */
        redisClusterNode *dstNode;
        redisClusterNodeIterator ni;
        redisClusterInitNodeIterator(&ni, cc);
        while ((dstNode = redisClusterNodeNext(&ni)) != NULL) {
            if (dstNode != srcNode)
                break;
        }
        assert(dstNode && dstNode != srcNode);
        int dstPort = dstNode->port;

        redisReply *reply, *replySrcId, *replyDstId;

        /* Get node id's */
        replySrcId = redisClusterCommandToNode(cc, srcNode, "CLUSTER MYID");
        CHECK_REPLY_TYPE(replySrcId, REDIS_REPLY_STRING);

        replyDstId = redisClusterCommandToNode(cc, dstNode, "CLUSTER MYID");
        CHECK_REPLY_TYPE(replyDstId, REDIS_REPLY_STRING);

        /* Migrate slot */
        reply = redisClusterCommandToNode(cc, srcNode,
                                          "CLUSTER SETSLOT %d MIGRATING %s",
                                          slot, replyDstId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
        reply = redisClusterCommandToNode(cc, dstNode,
                                          "CLUSTER SETSLOT %d IMPORTING %s",
                                          slot, replySrcId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
        reply = redisClusterCommandToNode(
            cc, srcNode, "MIGRATE 127.0.0.1 %d foo 0 5000", dstPort);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);

        /* Test ASK reply handling with OOM */
        for (int i = 0; i < 50; ++i) {
            prepare_allocation_test(cc, i);
            reply = redisClusterCommand(cc, "GET foo");
            assert(reply == NULL);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        /* Test ASK reply handling without OOM */
        prepare_allocation_test(cc, 50);
        reply = redisClusterCommand(cc, "GET foo");
        CHECK_REPLY_STR(cc, reply, "one");
        freeReplyObject(reply);

        /* Finalize the migration. Skip OOM testing during these steps by
         * allowing a high number of allocations. */
        prepare_allocation_test(cc, 1000);
        /* Fetch the nodes again, in case the slotmap has been reloaded. */
        srcNode = redisClusterGetNodeByKey(cc, "foo");
        dstNode = getNodeByPort(cc, dstPort);
        reply = redisClusterCommandToNode(
            cc, srcNode, "CLUSTER SETSLOT %d NODE %s", slot, replyDstId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
        reply = redisClusterCommandToNode(
            cc, dstNode, "CLUSTER SETSLOT %d NODE %s", slot, replyDstId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);

        /* Test MOVED reply handling with OOM */
        for (int i = 0; i < 34; ++i) {
            prepare_allocation_test(cc, i);
            reply = redisClusterCommand(cc, "GET foo");
            assert(reply == NULL);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        /* Test MOVED reply handling without OOM */
        prepare_allocation_test(cc, 34);
        reply = redisClusterCommand(cc, "GET foo");
        CHECK_REPLY_STR(cc, reply, "one");
        freeReplyObject(reply);

        /* MOVED triggers a slotmap update which currently replaces all cluster_node
         * objects. We can get the new objects by searching for its server ports.
         * This enables us to migrate the slot back to the original node. */
        srcNode = getNodeByPort(cc, srcPort);
        dstNode = getNodeByPort(cc, dstPort);

        /* Migrate back slot, required by the next testcase. Skip OOM testing
         * during these final steps by allowing a high number of allocations. */
        prepare_allocation_test(cc, 1000);
        reply = redisClusterCommandToNode(cc, dstNode,
                                          "CLUSTER SETSLOT %d MIGRATING %s",
                                          slot, replySrcId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
        reply = redisClusterCommandToNode(cc, srcNode,
                                          "CLUSTER SETSLOT %d IMPORTING %s",
                                          slot, replyDstId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
        reply = redisClusterCommandToNode(
            cc, dstNode, "MIGRATE 127.0.0.1 %d foo 0 5000", srcPort);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
        reply = redisClusterCommandToNode(
            cc, dstNode, "CLUSTER SETSLOT %d NODE %s", slot, replySrcId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
        reply = redisClusterCommandToNode(
            cc, srcNode, "CLUSTER SETSLOT %d NODE %s", slot, replySrcId->str);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);

        freeReplyObject(replySrcId);
        freeReplyObject(replyDstId);
    }

    redisClusterFree(cc);
    hiredisResetAllocators();
}

//------------------------------------------------------------------------------
// Async API
//------------------------------------------------------------------------------

typedef struct ExpectedResult {
    int type;
    char *str;
    bool disconnect;
} ExpectedResult;

// Callback for Redis connects and disconnects
void callbackExpectOk(const redisAsyncContext *ac, int status) {
    UNUSED(ac);
    assert(status == REDIS_OK);
}

// Callback for async commands, verifies the redisReply
void commandCallback(redisClusterAsyncContext *cc, void *r, void *privdata) {
    redisReply *reply = (redisReply *)r;
    ExpectedResult *expect = (ExpectedResult *)privdata;
    assert(reply != NULL);
    assert(reply->type == expect->type);
    assert(strcmp(reply->str, expect->str) == 0);

    if (expect->disconnect) {
        redisClusterAsyncDisconnect(cc);
    }
}

// Test of allocation handling in async context
void test_alloc_failure_handling_async(void) {
    int result;
    hiredisAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };
    // Override allocators
    hiredisSetAllocators(&ha);

    // Context init
    redisClusterAsyncContext *acc;
    {
        for (int i = 0; i < 2; ++i) {
            successfulAllocations = 0;
            acc = redisClusterAsyncContextInit();
            assert(acc == NULL);
        }
        successfulAllocations = 2;
        acc = redisClusterAsyncContextInit();
        assert(acc);
    }

    // Set callbacks
    {
        prepare_allocation_test_async(acc, 0);
        result = redisClusterAsyncSetConnectCallback(acc, callbackExpectOk);
        assert(result == REDIS_OK);
        result = redisClusterAsyncSetDisconnectCallback(acc, callbackExpectOk);
        assert(result == REDIS_OK);
    }

    // Add nodes
    {
        for (int i = 0; i < 9; ++i) {
            prepare_allocation_test(acc->cc, i);
            result = redisClusterSetOptionAddNodes(acc->cc, CLUSTER_NODE);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(acc->cc->errstr, "Out of memory");
        }

        prepare_allocation_test(acc->cc, 9);
        result = redisClusterSetOptionAddNodes(acc->cc, CLUSTER_NODE);
        assert(result == REDIS_OK);
    }

    // Connect
    {
        for (int i = 0; i < 126; ++i) {
            prepare_allocation_test(acc->cc, i);
            result = redisClusterConnect2(acc->cc);
            assert(result == REDIS_ERR);
        }

        prepare_allocation_test(acc->cc, 126);
        result = redisClusterConnect2(acc->cc);
        assert(result == REDIS_OK);
    }

    struct event_base *base = event_base_new();
    assert(base);

    successfulAllocations = 0;
    result = redisClusterLibeventAttach(acc, base);
    assert(result == REDIS_OK);

    // Async command 1
    ExpectedResult r1 = {.type = REDIS_REPLY_STATUS, .str = "OK"};
    {
        const char *cmd1 = "SET foo one";

        for (int i = 0; i < 38; ++i) {
            prepare_allocation_test_async(acc, i);
            result = redisClusterAsyncCommand(acc, commandCallback, &r1, cmd1);
            assert(result == REDIS_ERR);
            if (i != 36) {
                ASSERT_STR_EQ(acc->errstr, "Out of memory");
            } else {
                ASSERT_STR_EQ(acc->errstr, "Failed to attach event adapter");
            }
        }

        prepare_allocation_test_async(acc, 38);
        result = redisClusterAsyncCommand(acc, commandCallback, &r1, cmd1);
        ASSERT_MSG(result == REDIS_OK, acc->errstr);
    }

    // Async command 2
    ExpectedResult r2 = {
        .type = REDIS_REPLY_STRING, .str = "one", .disconnect = true};
    {
        const char *cmd2 = "GET foo";

        for (int i = 0; i < 15; ++i) {
            prepare_allocation_test_async(acc, i);
            result = redisClusterAsyncCommand(acc, commandCallback, &r2, cmd2);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(acc->errstr, "Out of memory");
        }

        /* Due to an issue in hiredis 1.0.0 iteration 15 is avoided.
           The issue (that triggers an assert) is corrected on master:
           https://github.com/redis/hiredis/commit/4bba72103c93eaaa8a6e07176e60d46ab277cf8a
         */
        prepare_allocation_test_async(acc, 16);
        result = redisClusterAsyncCommand(acc, commandCallback, &r2, cmd2);
        ASSERT_MSG(result == REDIS_OK, acc->errstr);
    }

    prepare_allocation_test_async(acc, 7);
    event_base_dispatch(base);
    redisClusterAsyncFree(acc);
    event_base_free(base);
}

int main(void) {

    test_alloc_failure_handling();
    test_alloc_failure_handling_async();

    return 0;
}

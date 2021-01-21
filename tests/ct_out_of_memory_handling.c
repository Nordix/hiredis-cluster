#include "hircluster.h"
#include "test_utils.h"
#include <assert.h>
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

void prepare_allocation_test(redisClusterContext *cc,
                             int _successfulAllocations) {
    successfulAllocations = _successfulAllocations;
    cc->err = 0;
    memset(cc->errstr, '\0', strlen(cc->errstr));
}

// Test of allocation handling
// The testcase will trigger allocation failures during API calls.
// It will start by triggering an allocation fault, and the next iteration
// will start with an successfull allocation and then a failing one,
// next iteration 2 successful and one failing allocation, and so on..
void test_cluster_communication() {
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
        for (int i = 0; i < 133; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterConnect2(cc);
            assert(result == REDIS_ERR);
        }

        prepare_allocation_test(cc, 133);
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

        for (int i = 0; i < 78; ++i) {
            prepare_allocation_test(cc, i);
            reply = (redisReply *)redisClusterCommand(cc, cmd);
            assert(reply == NULL);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        // Multi-key commands
        prepare_allocation_test(cc, 78);
        reply = (redisReply *)redisClusterCommand(cc, cmd);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Append command
    {
        redisReply *reply;
        const char *cmd = "SET foo one";

        for (int i = 0; i < 33; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        for (int i = 0; i < 4; ++i) {
            // Appended command lost when receiving error from hiredis
            // during a GetReply, needs a new append for each test loop
            prepare_allocation_test(cc, 33);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_OK);

            prepare_allocation_test(cc, i);
            result = redisClusterGetReply(cc, (void *)&reply);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        prepare_allocation_test(cc, 33);
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

        for (int i = 0; i < 70; ++i) {
            prepare_allocation_test(cc, i);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        for (int i = 0; i < 13; ++i) {
            prepare_allocation_test(cc, 70);
            result = redisClusterAppendCommand(cc, cmd);
            assert(result == REDIS_OK);

            prepare_allocation_test(cc, i);
            result = redisClusterGetReply(cc, (void *)&reply);
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        prepare_allocation_test(cc, 70);
        result = redisClusterAppendCommand(cc, cmd);
        assert(result == REDIS_OK);

        prepare_allocation_test(cc, 13);
        result = redisClusterGetReply(cc, (void *)&reply);
        assert(result == REDIS_OK);
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    redisClusterFree(cc);
    hiredisResetAllocators();
}

int main() {

    test_cluster_communication();

    return 0;
}

#include "hircluster.h"
#include "test_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

int successfulAllocations = 0;

// A configurable OOM failing malloc()
static void *hi_malloc_fail(size_t size) {
    if (successfulAllocations > 0) {
        --successfulAllocations;
        return malloc(size);
    }
    return NULL;
}

// A  configurable OOM failing calloc()
static void *hi_calloc_fail(size_t nmemb, size_t size) {
    if (successfulAllocations > 0) {
        --successfulAllocations;
        return calloc(nmemb, size);
    }
    return NULL;
}

// A  configurable OOM failing realloc()
static void *hi_realloc_fail(void *ptr, size_t size) {
    if (successfulAllocations > 0) {
        --successfulAllocations;
        return realloc(ptr, size);
    }
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

    // Set timeout
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

        for (int i = 0; i < 36; ++i) {
            prepare_allocation_test(cc, 0 + i);
            reply = (redisReply *)redisClusterCommand(cc, "SET key value");
            assert(reply == NULL);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        prepare_allocation_test(cc, 36);
        reply = (redisReply *)redisClusterCommand(cc, "SET key value");
        CHECK_REPLY_OK(cc, reply);
        freeReplyObject(reply);
    }

    // Append command
    {
        for (int i = 0; i < 33; ++i) {
            prepare_allocation_test(cc, 0 + i);
            result = redisClusterAppendCommand(cc, "SET foo one");
            assert(result == REDIS_ERR);
            ASSERT_STR_EQ(cc->errstr, "Out of memory");
        }

        prepare_allocation_test(cc, 33);
        result = redisClusterAppendCommand(cc, "SET foo one");
        assert(result == REDIS_OK);
    }

    redisClusterFree(cc);
    hiredisResetAllocators();
}

int main() {

    test_cluster_communication();

    return 0;
}

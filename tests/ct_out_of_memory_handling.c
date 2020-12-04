#include "hircluster.h"
#include "test_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"

// A OOM failing malloc()
static void *hi_malloc_fail(size_t size) {
    UNUSED(size);
    return NULL;
}

// A OOM failing calloc()
static void *hi_calloc_fail(size_t nmemb, size_t size) {
    UNUSED(nmemb);
    UNUSED(size);
    return NULL;
}

// A OOM failing realloc()
static void *hi_realloc_fail(void *ptr, size_t size) {
    UNUSED(ptr);
    UNUSED(size);
    return NULL;
}

// Reset the error string (between tests)
void reset_cluster_errors(redisClusterContext *cc) {
    cc->err = 0;
    memset(cc->errstr, '\0', strlen(cc->errstr));
}

// Test OOM handling in first allocation in:
// redisClusterContextInit();
void test_context_init() {
    hiredisAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };

    // Override allocators
    hiredisSetAllocators(&ha);
    {
        redisClusterContext *cc = redisClusterContextInit();
        assert(cc == NULL);
    }
    hiredisResetAllocators();
}

// Test OOM handling in first allocation in:
// redisClusterSetOptionXXXX
void test_setoptions() {
    hiredisAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };

    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);
    ASSERT_STR_EQ(cc->errstr, "");

    // Override allocators
    hiredisSetAllocators(&ha);
    {
        int result;
        result = redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
        assert(result == REDIS_ERR);
        ASSERT_STR_STARTS_WITH(cc->errstr, "servers address is error");

        reset_cluster_errors(cc);

        result = redisClusterSetOptionAddNode(cc, CLUSTER_NODE);
        assert(result == REDIS_ERR);
        ASSERT_STR_EQ(cc->errstr, "Out of memory");

        reset_cluster_errors(cc);

        struct timeval timeout = {0, 500000};
        result = redisClusterSetOptionConnectTimeout(cc, timeout);
        assert(result == REDIS_ERR);
        ASSERT_STR_EQ(cc->errstr, "Out of memory");

        reset_cluster_errors(cc);

        result = redisClusterSetOptionTimeout(cc, timeout);
        assert(result == REDIS_ERR);
        ASSERT_STR_EQ(cc->errstr, "Out of memory");
    }
    hiredisResetAllocators();

    redisClusterFree(cc);
}

// Test OOM handling in first allocation in:
// redisClusterConnect2()
void test_cluster_connect() {
    hiredisAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };
    struct timeval timeout = {0, 500000};

    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);
    redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
    redisClusterSetOptionConnectTimeout(cc, timeout);
    ASSERT_STR_EQ(cc->errstr, "");

    // Override allocators
    hiredisSetAllocators(&ha);
    {
        int result;
        result = redisClusterConnect2(cc);
        assert(result == REDIS_ERR);
        ASSERT_STR_EQ(cc->errstr, "Out of memory");
    }
    hiredisResetAllocators();

    redisClusterFree(cc);
}

// Test OOM handling in first allocation in:
// redisClusterCommand()
void test_cluster_command() {
    hiredisAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };

    struct timeval timeout = {0, 500000};

    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);
    redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
    redisClusterSetOptionConnectTimeout(cc, timeout);

    int result;
    result = redisClusterConnect2(cc);
    ASSERT_MSG(result == REDIS_OK, cc->errstr);
    ASSERT_STR_EQ(cc->errstr, "");

    // Override allocators
    hiredisSetAllocators(&ha);
    {
        redisReply *reply;
        reply = (redisReply *)redisClusterCommand(cc, "SET key value");
        assert(reply == NULL);

        ASSERT_STR_EQ(cc->errstr, "Out of memory");
    }
    hiredisResetAllocators();

    redisClusterFree(cc);
}

// Test OOM handling in first allocation in:
// redisClusterAppendCommand()
void test_cluster_append_command() {
    hiredisAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };

    struct timeval timeout = {0, 500000};

    redisClusterContext *cc = redisClusterContextInit();
    assert(cc);
    redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
    redisClusterSetOptionConnectTimeout(cc, timeout);

    int result;
    result = redisClusterConnect2(cc);
    ASSERT_MSG(result == REDIS_OK, cc->errstr);
    ASSERT_STR_EQ(cc->errstr, "");

    // Override allocators
    hiredisSetAllocators(&ha);
    {
        result = redisClusterAppendCommand(cc, "SET foo one");
        assert(result == REDIS_ERR);
        ASSERT_STR_EQ(cc->errstr, "Out of memory");
    }
    hiredisResetAllocators();

    redisClusterFree(cc);
}

int main() {
    // Test the handling of an out-of-memory situation
    // in the first allocation done in the API functions.
    test_context_init();
    test_setoptions();
    test_cluster_connect();
    test_cluster_command();
    test_cluster_append_command();

    return 0;
}

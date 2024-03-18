#include "adapters/libevent.h"
#include "hircluster.h"
#include "test_utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define CLUSTER_NODE "127.0.0.1:7000"

void getCallback(redisClusterAsyncContext *acc, void *r, void *privdata) {
    UNUSED(privdata);
    redisReply *reply = (redisReply *)r;
    ASSERT_MSG(reply != NULL, acc->errstr);

    /* Disconnect after receiving the first reply to GET */
    redisClusterAsyncDisconnect(acc);
}

void setCallback(redisClusterAsyncContext *acc, void *r, void *privdata) {
    UNUSED(privdata);
    redisReply *reply = (redisReply *)r;
    ASSERT_MSG(reply != NULL, acc->errstr);
}

void connectCallback(const redisAsyncContext *ac, int status) {
    ASSERT_MSG(status == REDIS_OK, ac->errstr);
    printf("Connected to %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

#ifndef HIRCLUSTER_NO_NONCONST_CONNECT_CB
void connectCallbackNC(redisAsyncContext *ac, int status) {
    UNUSED(ac);
    UNUSED(status);
    /* The testcase expects a failure during registration of this
       non-const connect callback and it should never be called. */
    assert(0);
}
#endif

void disconnectCallback(const redisAsyncContext *ac, int status) {
    ASSERT_MSG(status == REDIS_OK, ac->errstr);
    printf("Disconnected from %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

void eventCallback(const redisClusterContext *cc, int event, void *privdata) {
    (void)cc;
    redisClusterAsyncContext *acc = (redisClusterAsyncContext *)privdata;

    /* We send our commands when the client is ready to accept commands. */
    if (event == HIRCLUSTER_EVENT_READY) {
        int status;
        status = redisClusterAsyncCommand(acc, setCallback, (char *)"ID",
                                          "SET key12345 value");
        ASSERT_MSG(status == REDIS_OK, acc->errstr);

        /* This command will trigger a disconnect in its reply callback. */
        status = redisClusterAsyncCommand(acc, getCallback, (char *)"ID",
                                          "GET key12345");
        ASSERT_MSG(status == REDIS_OK, acc->errstr);

        status = redisClusterAsyncCommand(acc, setCallback, (char *)"ID",
                                          "SET key23456 value2");
        ASSERT_MSG(status == REDIS_OK, acc->errstr);

        status = redisClusterAsyncCommand(acc, getCallback, (char *)"ID",
                                          "GET key23456");
        ASSERT_MSG(status == REDIS_OK, acc->errstr);
    }
}

int main(void) {

    redisClusterAsyncContext *acc = redisClusterAsyncContextInit();
    assert(acc);

    int status;
    status = redisClusterAsyncSetConnectCallback(acc, connectCallback);
    assert(status == REDIS_OK);
    status = redisClusterAsyncSetConnectCallback(acc, connectCallback);
    assert(status == REDIS_ERR); /* Re-registration not accepted */

#ifndef HIRCLUSTER_NO_NONCONST_CONNECT_CB
    status = redisClusterAsyncSetConnectCallbackNC(acc, connectCallbackNC);
    assert(status == REDIS_ERR); /* Re-registration not accepted */
#endif

    status = redisClusterAsyncSetDisconnectCallback(acc, disconnectCallback);
    assert(status == REDIS_OK);
    status = redisClusterSetEventCallback(acc->cc, eventCallback, acc);
    assert(status == REDIS_OK);
    status = redisClusterSetOptionAddNodes(acc->cc, CLUSTER_NODE);
    assert(status == REDIS_OK);

    /* Expect error when connecting without an attached event library. */
    status = redisClusterAsyncConnect2(acc);
    assert(status == REDIS_ERR);

    struct event_base *base = event_base_new();
    status = redisClusterLibeventAttach(acc, base);
    assert(status == REDIS_OK);

    status = redisClusterAsyncConnect2(acc);
    assert(status == REDIS_OK);

    event_base_dispatch(base);

    redisClusterAsyncFree(acc);
    event_base_free(base);
    return 0;
}

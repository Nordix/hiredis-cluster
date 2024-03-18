/*
 * Simple example how to enable client tracking to implement client side caching.
 * Tracking can be enabled via a registered connect callback and invalidation
 * messages are received via the registered push callback.
 * The disconnect callback should also be used as an indication of invalidation.
 */
#include <hiredis_cluster/adapters/libevent.h>
#include <hiredis_cluster/hircluster.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLUSTER_NODE "127.0.0.1:7000"
#define KEY "key:1"

void pushCallback(redisAsyncContext *ac, void *r);
void setCallback(redisClusterAsyncContext *acc, void *r, void *privdata);
void getCallback1(redisClusterAsyncContext *acc, void *r, void *privdata);
void getCallback2(redisClusterAsyncContext *acc, void *r, void *privdata);
void modifyKey(const char *key, const char *value);

/* The connect callback enables RESP3 and client tracking.
   The non-const connect callback is used since we want to
   set the push callback in the hiredis context. */
void connectCallbackNC(redisAsyncContext *ac, int status) {
    assert(status == REDIS_OK);
    redisAsyncSetPushCallback(ac, pushCallback);
    redisAsyncCommand(ac, NULL, NULL, "HELLO 3");
    redisAsyncCommand(ac, NULL, NULL, "CLIENT TRACKING ON");
    printf("Connected to %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);
}

/* The event callback issues a 'SET' command when the client is ready to accept
   commands. A reply is expected via a call to 'setCallback()' */
void eventCallback(const redisClusterContext *cc, int event, void *privdata) {
    (void)cc;
    redisClusterAsyncContext *acc = (redisClusterAsyncContext *)privdata;

    /* We send our commands when the client is ready to accept commands. */
    if (event == HIRCLUSTER_EVENT_READY) {
        printf("Client is ready to accept commands\n");

        int status =
            redisClusterAsyncCommand(acc, setCallback, NULL, "SET %s 1", KEY);
        assert(status == REDIS_OK);
    }
}

/* Message callback for 'SET' commands. Issues a 'GET' command and a reply is
   expected as a call to 'getCallback1()' */
void setCallback(redisClusterAsyncContext *acc, void *r, void *privdata) {
    (void)privdata;
    redisReply *reply = (redisReply *)r;
    assert(reply != NULL);
    printf("Callback for 'SET', reply: %s\n", reply->str);

    int status =
        redisClusterAsyncCommand(acc, getCallback1, NULL, "GET %s", KEY);
    assert(status == REDIS_OK);
}

/* Message callback for the first 'GET' command. Modifies the key to
   trigger Redis to send a key invalidation message and then sends another
   'GET' command. The invalidation message is received via the registered
   push callback. */
void getCallback1(redisClusterAsyncContext *acc, void *r, void *privdata) {
    (void)privdata;
    redisReply *reply = (redisReply *)r;
    assert(reply != NULL);

    printf("Callback for first 'GET', reply: %s\n", reply->str);

    /* Modify the key from another client which will invalidate a cached value.
       Redis will send an invalidation message via a push message. */
    modifyKey(KEY, "99");

    int status =
        redisClusterAsyncCommand(acc, getCallback2, NULL, "GET %s", KEY);
    assert(status == REDIS_OK);
}

/* Push message callback handling invalidation messages. */
void pushCallback(redisAsyncContext *ac, void *r) {
    redisReply *reply = r;
    if (!(reply->type == REDIS_REPLY_PUSH && reply->elements == 2 &&
          reply->element[0]->type == REDIS_REPLY_STRING &&
          !strncmp(reply->element[0]->str, "invalidate", 10) &&
          reply->element[1]->type == REDIS_REPLY_ARRAY)) {
        /* Not an 'invalidate' message. Ignore. */
        return;
    }
    redisReply *payload = reply->element[1];
    size_t i;
    for (i = 0; i < payload->elements; i++) {
        redisReply *key = payload->element[i];
        if (key->type == REDIS_REPLY_STRING)
            printf("Invalidate key '%.*s'\n", (int)key->len, key->str);
        else if (key->type == REDIS_REPLY_NIL)
            printf("Invalidate all\n");
    }
}

/* Message callback for 'GET' commands. Exits program. */
void getCallback2(redisClusterAsyncContext *acc, void *r, void *privdata) {
    (void)privdata;
    redisReply *reply = (redisReply *)r;
    assert(reply != NULL);

    printf("Callback for second 'GET', reply: %s\n", reply->str);

    /* Exit the eventloop after a couple of sent commands. */
    redisClusterAsyncDisconnect(acc);
}

/* A disconnect callback should invalidate all cached keys. */
void disconnectCallback(const redisAsyncContext *ac, int status) {
    assert(status == REDIS_OK);
    printf("Disconnected from %s:%d\n", ac->c.tcp.host, ac->c.tcp.port);

    printf("Invalidate all\n");
}

/* Helper to modify keys using a separate client. */
void modifyKey(const char *key, const char *value) {
    printf("Modify key: '%s'\n", key);
    redisClusterContext *cc = redisClusterContextInit();
    int status = redisClusterSetOptionAddNodes(cc, CLUSTER_NODE);
    assert(status == REDIS_OK);
    status = redisClusterConnect2(cc);
    assert(status == REDIS_OK);

    redisReply *reply = redisClusterCommand(cc, "SET %s %s", key, value);
    assert(reply != NULL);
    freeReplyObject(reply);

    redisClusterFree(cc);
}

int main(int argc, char **argv) {
    redisClusterAsyncContext *acc = redisClusterAsyncContextInit();
    assert(acc);

    int status;
    status = redisClusterAsyncSetConnectCallbackNC(acc, connectCallbackNC);
    assert(status == REDIS_OK);
    status = redisClusterAsyncSetDisconnectCallback(acc, disconnectCallback);
    assert(status == REDIS_OK);
    status = redisClusterSetEventCallback(acc->cc, eventCallback, acc);
    assert(status == REDIS_OK);
    status = redisClusterSetOptionAddNodes(acc->cc, CLUSTER_NODE);
    assert(status == REDIS_OK);

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

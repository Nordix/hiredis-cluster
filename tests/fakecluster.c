#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/thread.h>
#include <event2/util.h>

#include "adlist.h"
#include "hiarray.h"

#define UNUSED(x) (void)(x)

pthread_t tid = 0;
struct event_base *base;
struct hiarray *listeners;
hilist *bufferevents;

static struct evconnlistener *create_listener(int port);
static void *thread_func(void *arg);
static void listener_cb(struct evconnlistener *, evutil_socket_t,
                        struct sockaddr *, int socklen, void *);
static void conn_event_cb(struct bufferevent *, short, void *);
static void conn_read_cb(struct bufferevent *, void *);

// Start listening for connections on given port
void fakecluster_start(int first_port, int number_of_nodes) {
    // Due to CLUSTER NODES static response
    assert(first_port == 4455);
    assert(number_of_nodes == 6);

    listeners = hiarray_create(1, sizeof(struct evconnlistener *));
    bufferevents = listCreate();

    int ret;
    ret = evthread_use_pthreads();
    assert(ret == 0);

    base = event_base_new();
    assert(base);

    ret = pthread_create(&tid, NULL, thread_func, NULL);
    assert(ret == 0);

    int port = first_port;
    for (int i = 0; i < number_of_nodes; ++i) {
        struct evconnlistener **l = hiarray_push(listeners);
        assert(l);
        *l = create_listener(port + i);
    }
}

// Close sockets and terminate eventloop
void fakecluster_stop() {
    event_base_loopbreak(base);

    pthread_join(tid, NULL);

    // Cleanup libevent data

    listIter li;
    listRewind(bufferevents, &li);

    listNode *ln;
    while ((ln = listNext(&li))) {
        bufferevent_free((struct bufferevent *)ln->value);
        listDelNode(bufferevents, ln);
    }
    listRelease(bufferevents);

    while (hiarray_n(listeners)) {
        struct evconnlistener **l = hiarray_pop(listeners);
        evconnlistener_free(*l);
    }
    hiarray_destroy(listeners);

    event_base_free(base);
    libevent_global_shutdown();
}

// Register a server socket
static struct evconnlistener *create_listener(int port) {
    struct evconnlistener *listener;

    printf("Start listening on: :%d\n", port);
    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);
    listener =
        evconnlistener_new_bind(base, listener_cb, (void *)base,
                                LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
                                (struct sockaddr *)&sin, sizeof(sin));
    assert(listener);
    return listener;
}

// Thread that runs the eventloop
static void *thread_func(void *arg) {
    UNUSED(arg);
    event_base_loop(base, EVLOOP_NO_EXIT_ON_EMPTY);
    return NULL;
}

// Handle a connecting client
static void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                        struct sockaddr *sa, int socklen, void *user_data) {
    UNUSED(listener);
    UNUSED(socklen);
    UNUSED(user_data);
    struct event_base *base = user_data;
    struct bufferevent *bev;

    struct sockaddr_in *addr_in = (struct sockaddr_in *)sa;
    printf("Accepting %s:%d\n", inet_ntoa(addr_in->sin_addr),
           addr_in->sin_port); // IPv4 only!

    bev = bufferevent_socket_new(base, fd,
                                 BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
    assert(bev);

    bufferevent_setcb(bev, conn_read_cb, NULL, conn_event_cb, NULL);
    bufferevent_enable(bev, EV_READ | EV_WRITE);

    // Store bufferevent to be able to close connections
    listAddNodeTail(bufferevents, bev);
}

// Handle connection error events
static void conn_event_cb(struct bufferevent *bev, short events,
                          void *user_data) {
    UNUSED(user_data);
    if (events & BEV_EVENT_EOF) {
        printf("Connection closed.\n");
    } else if (events & BEV_EVENT_ERROR) {
        printf("Error: %s\n", strerror(errno));
    }

    // Remove closed bufferevent from book-keeping
    listNode *ln = listSearchKey(bufferevents, bev);
    assert(ln);
    listDelNode(bufferevents, ln);

    bufferevent_free(bev);
}

// Handle data sent by a client
static void conn_read_cb(struct bufferevent *bev, void *user_data) {
    UNUSED(user_data);

    struct evbuffer *input = bufferevent_get_input(bev);

    size_t len = evbuffer_get_length(input);
    char *data = (char *)malloc(len);
    evbuffer_copyout(input, data, len);

    // Very-very simple parsing, needs to be improved!
    if (memcmp(data, "*2\r\n$7\r\nCLUSTER\r\n$5\r\nNODES\r\n", 28) == 0) {
        // CLUSTER NODES

        // clang-format off
        char *cluster_nodes =
        "5cd86adfc69683c4a495d26276e68e1b111fc467 127.0.0.1:4455@40001 myself,master - 0 1605808574000 1 connected 0-5460\n"
        "c1894bfdb935d811207ff6e2a2c53f3a5c31cc1e 127.0.0.1:4460@40002 master - 0 1605808573000 2 connected 5461-10922\n"
        "f0ee4b54ed31850840574452500591e3ea09de21 127.0.0.1:4459@40003 master - 0 1605808573891 3 connected 10923-16383\n"
        "33ca628c512a5159f168a210ec2c1efef3e0bd5c 127.0.0.1:4456@40005 slave f0ee4b54ed31850840574452500591e3ea09de21 0 1605808573391 5 connected\n"
        "985075cec85e5ced0df9d0a70c24674f88966541 127.0.0.1:4457@40006 slave 5cd86adfc69683c4a495d26276e68e1b111fc467 0 1605808574393 6 connected\n"
        "ec43d216912ea76438d0db6f56e077d53d32d33b 127.0.0.1:4458@40004 slave c1894bfdb935d811207ff6e2a2c53f3a5c31cc1e 0 1605808573591 4 connected\n";
        // clang-format on

        char msg[800];
        int n = sprintf(msg, "$%ld\r\n%s\r\n", strlen(cluster_nodes),
                        cluster_nodes);
        assert(n);
        bufferevent_write(bev, msg, strlen(msg));
    } else if (memcmp(data, "*3\r\n$3\r\nSET\r\n", 13) == 0) {
        // SET
        char *msg = "+OK\r\n";
        bufferevent_write(bev, msg, strlen(msg));
    } else {
        printf("UNKNOWN COMMAND: '%s'", data);
        char *msg = "-ERR unknown command\r\n";
        bufferevent_write(bev, msg, strlen(msg));
    }

    free(data);
}

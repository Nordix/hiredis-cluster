# Hiredis-cluster

Hiredis-cluster is a C client library for cluster deployments of the
[Redis](http://redis.io/) database.

Hiredis-cluster is using [Hiredis](https://github.com/redis/hiredis) for the
connections to each Redis node.

Hiredis-cluster is a fork of Hiredis-vip, with the following improvements:

* The C library `hiredis` is an external dependency rather than a builtin part
  of the cluster client, meaning that the latest `hiredis` can be used.
* Support for SSL/TLS introduced in Redis 6
* Support for IPv6
* Support authentication using AUTH
* Uses CMake (3.11+) as the primary build system, but optionally Make can be used directly
* Code style guide (using clang-format)
* Improved testing
* Memory leak corrections and allocation failure handling
* Low-level API for sending commands to specific node

## Features

* Redis Cluster
    * Connect to a Redis cluster and run commands.

* Multi-key commands
    * Support `MSET`, `MGET` and `DEL`.
    * Multi-key commands will be processed and sent to slot owning nodes.
      (This breaks the atomicity of the commands if the keys reside on different
      nodes so if atomicity is important, use these only with keys in the same
      cluster slot.)

* Pipelining
    * Send multiple commands at once to speed up queries.
    * Supports multi-key commands described in above bullet.

* Asynchronous API
    * Send commands asynchronously and let a callback handle the response.
    * Needs an external event loop system that can be attached using an adapter.

* SSL/TLS
    * Connect to Redis nodes using SSL/TLS (supported from Redis 6)

* IPv6
    * Handles clusters on IPv6 networks

## Build instructions

Prerequisites:

* A C compiler (GCC or Clang)
* CMake and GNU Make (but see [Alternative build using Makefile
  directly](#alternative-build-using-makefile-directly) below for how to build
  without CMake)
* [hiredis >= v1.0.0](https://github.com/redis/hiredis); downloaded automatically by
  default, see [build options](#build-options) to disable.
* [libevent](https://libevent.org/) (`libevent-dev` in Debian); can be avoided
  if building without tests (DISABLE_TESTS=ON)
* OpenSSL (`libssl-dev` in Debian) if building with TLS support

Hiredis-cluster will be built as a shared library `libhiredis_cluster.so` and
it depends on the hiredis shared library `libhiredis.so`.

When SSL/TLS support is enabled an extra library `libhiredis_cluster_ssl.so`
is built, which depends on the hiredis SSL support library `libhiredis_ssl.a`.

A user project that needs SSL/TLS support should link to both `libhiredis_cluster.so`
and `libhiredis_cluster_ssl.so` to enable the SSL/TLS configuration API.

```sh
$ mkdir build; cd build
$ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_SSL=ON ..
$ make
```

### Build options

The following CMake options are available:

* `DOWNLOAD_HIREDIS`
  * `OFF` CMake will search for an already installed hiredis (for example the
    the Debian package `libhiredis-dev`) for header files and linkage.
  * `ON` (default) hiredis will be downloaded from
    [Github](https://github.com/redis/hiredis), built and installed locally in
    the build folder.
* `ENABLE_SSL`
  * `OFF` (default)
  * `ON` Enable SSL/TLS support and build its tests (also affect hiredis when
    `DOWNLOAD_HIREDIS=ON`).
* `DISABLE_TESTS`
  * `OFF` (default)
  * `ON` Disable compilation of tests (also affect hiredis when
    `DOWNLOAD_HIREDIS=ON`).
* `ENABLE_IPV6_TESTS`
  * `OFF` (default)
  * `ON` Enable IPv6 tests. Requires that IPv6 is
    [setup](https://docs.docker.com/config/daemon/ipv6/) in Docker.
* `ENABLE_COVERAGE`
  * `OFF` (default)
  * `ON` Compile using build flags that enables the GNU coverage tool `gcov`
    to provide test coverage information. This CMake option also enables a new
    build target `coverage` to generate a test coverage report using
    [gcovr](https://gcovr.com/en/stable/index.html).
* `USE_SANITIZER`
   Compile using a specific sanitizer that detect issues. The value of this
   option specifies which sanitizer to activate, but it depends on support in the
   [compiler](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html#index-fsanitize_003daddress).
   Common option values are: `address`, `thread`, `undefined`, `leak`

Options needs to be set with the `-D` flag when generating makefiles, e.g.

`cmake -DENABLE_SSL=ON -DUSE_SANITIZER=address ..`

### Build details

The build uses CMake's [find_package](https://cmake.org/cmake/help/latest/command/find_package.html#search-procedure)
to search for a `hiredis` installation. CMake will search for a `hiredis`
installation in the default paths, searching for a file called `hiredis-config.cmake`.
The default search path can be altered via `CMAKE_PREFIX_PATH` or
as described in the CMake docs; a specific path can be set using a flag like:
`-Dhiredis_DIR:PATH=${MY_DIR}/hiredis/share/hiredis`

See `examples/using_cmake_separate/build.sh` or
`examples/using_cmake_externalproject/build.sh` for alternative CMake builds.

### Alternative build using Makefile directly

When a simpler build setup is preferred a provided Makefile can be used directly
when building. A benefit of this, instead of using CMake, is that it also provides
a static library, a similar limitation exists in the CMake files in hiredis v1.0.0.

The only option that exists in the Makefile is to enable SSL/TLS support via `USE_SSL=1`

By default the hiredis library (and headers) installed on the system is used,
but alternative installations can be used by defining the compiler flags
`CFLAGS` and `LDFLAGS`.

See [`examples/using_make/build.sh`](examples/using_make/build.sh) for an
example build using an alternative hiredis installation.

Build failures like
`hircluster_ssl.h:33:10: fatal error: hiredis/hiredis_ssl.h: No such file or directory`
indicates that hiredis is not installed on the system, or that a given `CFLAGS` is wrong.
Use the previous mentioned build example as reference.

### Running the tests

Prerequisites:

* Perl with [JSON module](https://metacpan.org/pod/JSON).
  Can be installed using `sudo cpan JSON`.
* [Docker](https://docs.docker.com/engine/install/)

Some tests needs a Redis cluster and that can be setup by the make targets
`start`/`stop`. The clusters will be setup using Docker and it may take a while
for them to be ready and accepting requests. Run `make start` to start the
clusters and then wait a few seconds before running `make test`.
To stop the running cluster containers run `make stop`.

```sh
$ make start
$ make test
$ make stop
```

If you want to set up the Redis clusters manually they should run on localhost
using following access ports:

| Cluster type                                        | Access port |
| ----------------------------------                  | -------:    |
| IPv4                                                | 7000        |
| IPv4, authentication needed, password: `secretword` | 7100        |
| IPv6                                                | 7200        |
| IPv4, using TLS/SSL                                 | 7300        |

## Quick usage

## Cluster synchronous API

### Connecting

The function `redisClusterContextInit` is used to create a `redisClusterContext`.
The context is where the state for connections is kept.

The function `redisClusterSetOptionAddNodes` is used to add one or many Redis Cluster addresses.

The functions `redisClusterSetOptionUsername` and
`redisClusterSetOptionPassword` are used to configure authentication, causing
the AUTH command to be sent on every new connection to Redis.

For more options, see the file [`hircluster.h`](hircluster.h).

The function `redisClusterConnect2` is used to connect to the Redis Cluster.

The `redisClusterContext` struct has an integer `err` field that is non-zero when the connection is
in an error state. The field `errstr` will contain a string with a description of the error.
After trying to connect to Redis using `redisClusterContext` you should check the `err` field to see
if establishing the connection was successful:
```c
redisClusterContext *cc = redisClusterContextInit();
redisClusterSetOptionAddNodes(cc, "127.0.0.1:6379,127.0.0.1:6380");
redisClusterConnect2(cc);
if (cc != NULL && cc->err) {
    printf("Error: %s\n", cc->errstr);
    // handle error
}
```

#### Events per cluster context

There is a hook to get notified when certain events occur.

```c
int redisClusterSetEventCallback(redisClusterContext *cc,
                                 void(fn)(const redisClusterContext *cc, int event,
                                          void *privdata),
                                 void *privdata);
```

The callback is called with `event` set to one of the following values:

* `HIRCLUSTER_EVENT_SLOTMAP_UPDATED` when the slot mapping has been updated;
* `HIRCLUSTER_EVENT_READY` when the slot mapping has been fetched for the first
  time and the client is ready to accept commands, useful when initiating the
  client with `redisClusterAsyncConnect2()` where a client is not immediately
  ready after a successful call;
* `HIRCLUSTER_EVENT_FREE_CONTEXT` when the cluster context is being freed, so
  that the user can free the event privdata.

#### Events per connection

There is a hook to get notified about connect and reconnect attempts.
This is useful for applying socket options or access endpoint information for a connection to a particular node.
The callback is registered using the following function:

```c
int redisClusterSetConnectCallback(redisClusterContext *cc,
                                   void(fn)(const redisContext *c, int status));
```

The callback is called just after connect, before TLS handshake and Redis authentication.

On successful connection, `status` is set to `REDIS_OK` and the redisContext
(defined in hiredis.h) can be used, for example, to see which IP and port it's
connected to or to set socket options directly on the file descriptor which can
be accessed as `c->fd`.

On failed connection attempt, this callback is called with `status` set to
`REDIS_ERR`. The `err` field in the `redisContext` can be used to find out
the cause of the error.

### Sending commands

The function `redisClusterCommand` takes a format similar to printf.
In the simplest form it is used like:
```c
reply = redisClusterCommand(clustercontext, "SET foo bar");
```

The specifier `%s` interpolates a string in the command, and uses `strlen` to
determine the length of the string:
```c
reply = redisClusterCommand(clustercontext, "SET foo %s", value);
```
Internally, hiredis-cluster splits the command in different arguments and will
convert it to the protocol used to communicate with Redis.
One or more spaces separates arguments, so you can use the specifiers
anywhere in an argument:
```c
reply = redisClusterCommand(clustercontext, "SET key:%s %s", myid, value);
```

Commands will be sent to the cluster node that the client perceives handling the given key.
If the cluster topology has changed the Redis node might respond with a redirection error
which the client will handle, update its slotmap and resend the command to correct node.
The reply will in this case arrive from the correct node.

If a node is unreachable, for example if the command times out or if the connect
times out, it can indicated that there has been a failover and the node is no
longer part of the cluster. In this case, `redisClusterCommand` returns NULL and
sets `err` and `errstr` on the cluster context, but additionally, hiredis
cluster schedules a slotmap update to be performed when the next command is
sent. That means that if you try the same command again, there is a good chance
the command will be sent to another node and the command may succeed.

### Sending multi-key commands

Hiredis-cluster supports mget/mset/del multi-key commands.
The command will be splitted per slot and sent to correct Redis nodes.

Example:
```c
reply = redisClusterCommand(clustercontext, "mget %s %s %s %s", key1, key2, key3, key4);
```

### Sending commands to a specific node

When there is a need to send commands to a specific node, the following low-level API can be used.

```c
reply = redisClusterCommandToNode(clustercontext, node, "DBSIZE");
```

This function handles printf like arguments similar to `redisClusterCommand()`, but will
only attempt to send the command to the given node and will not perform redirects or retries.

If the command times out or the connection to the node fails, a slotmap update
is scheduled to be performed when the next command is sent.
`redisClusterCommandToNode` also performs a slotmap update if it has previously
been scheduled.

### Teardown

To disconnect and free the context the following function can be used:
```c
void redisClusterFree(redisClusterContext *cc);
```
This function closes the sockets and deallocates the context.

### Cluster pipelining

The function `redisClusterGetReply` is exported as part of the Hiredis API and can be used
when a reply is expected on the socket. To pipeline commands, the only things that needs
to be done is filling up the output buffer. For this cause, the following commands can be used that
are identical to the `redisClusterCommand` family, apart from not returning a reply:
```c
int redisClusterAppendCommand(redisClusterContext *cc, const char *format, ...);
int redisClusterAppendCommandArgv(redisClusterContext *cc, int argc, const char **argv);

/* Send a command to a specific cluster node */
int redisClusterAppendCommandToNode(redisClusterContext *cc, redisClusterNode *node,
                                    const char *format, ...);
```
After calling either function one or more times, `redisClusterGetReply` can be used to receive the
subsequent replies. The return value for this function is either `REDIS_OK` or `REDIS_ERR`, where
the latter means an error occurred while reading a reply. Just as with the other commands,
the `err` field in the context can be used to find out what the cause of this error is.
```c
void redisClusterReset(redisClusterContext *cc);
```
Warning: You must call `redisClusterReset` function after one pipelining anyway.

Warning: Calling `redisClusterReset` without pipelining first will reset all Redis connections.

The following examples shows a simple cluster pipeline:
```c
redisReply *reply;
redisClusterAppendCommand(clusterContext,"SET foo bar");
redisClusterAppendCommand(clusterContext,"GET foo");
redisClusterGetReply(clusterContext,&reply); // reply for SET
freeReplyObject(reply);
redisClusterGetReply(clusterContext,&reply); // reply for GET
freeReplyObject(reply);
redisClusterReset(clusterContext);
```

## Cluster asynchronous API

Hiredis-cluster comes with an asynchronous cluster API that works with many event systems.
Currently there are adapters that enables support for `libevent`, `libev`, `libuv`, `glib`
and Redis Event Library (`ae`). For usage examples, see the test programs with the different
event libraries `tests/ct_async_{libev,libuv,glib}.c`.

The hiredis library has adapters for additional event systems that easily can be adapted
for hiredis-cluster as well.

### Connecting

There are two alternative ways to initiate a cluster client which also determines
how the client behaves during the initial connect.

The first alternative is to use the function `redisClusterAsyncConnect`, which initially
connects to the cluster in a blocking fashion and waits for the slotmap before returning.
Any command sent by the user thereafter will create a new non-blocking connection,
unless a non-blocking connection already exists to the destination.
The function returns a pointer to a newly created `redisClusterAsyncContext` struct and
its `err` field should be checked to make sure the initial slotmap update was successful.

```c
// Insufficient error handling for brevity.
redisClusterAsyncContext *acc = redisClusterAsyncConnect("127.0.0.1:6379", HIRCLUSTER_FLAG_NULL);
if (acc->err) {
    printf("error: %s\n", acc->errstr);
    exit(1);
}

// Attach an event engine. In this example we use libevent.
struct event_base *base = event_base_new();
redisClusterLibeventAttach(acc, base);
```

The second alternative is to use `redisClusterAsyncContextInit` and `redisClusterAsyncConnect2`
which avoids the initial blocking connect. This connection alternative requires an attached
event engine when `redisClusterAsyncConnect2` is called, but the connect and the initial
slotmap update is done in a non-blocking fashion.

This means that commands sent directly after `redisClusterAsyncConnect2` may fail
because the initial slotmap has not yet been retrieved and the client doesn't know which
cluster node to send the command to. You may use the [eventCallback](#events-per-cluster-context)
to be notified when the slotmap is updated and the client is ready to accept commands.
An crude example of using the eventCallback can be found in [this testcase](tests/ct_async.c).

```c
// Insufficient error handling for brevity.
redisClusterAsyncContext *acc = redisClusterAsyncContextInit();

// Add a cluster node address for the initial connect.
redisClusterSetOptionAddNodes(acc->cc, "127.0.0.1:6379");

// Attach an event engine. In this example we use libevent.
struct event_base *base = event_base_new();
redisClusterLibeventAttach(acc, base);

if (redisClusterAsyncConnect2(acc) != REDIS_OK) {
    printf("error: %s\n", acc->errstr);
    exit(1);
}
```

#### Events per cluster context

Use [`redisClusterSetEventCallback`](#events-per-cluster-context) with `acc->cc`
as the context to get notified when certain events occur.

#### Events per connection

Because the connections that will be created are non-blocking,
the kernel is not able to instantly return if the specified
host and port is able to accept a connection.
Instead, use a connect callback to be notified when a connection
is established or failed.
Similarily, a disconnect callback can be used to be notified about
a disconnected connection (either because of an error or per user request).
The callbacks are installed using the following functions:

```c
int redisClusterAsyncSetConnectCallback(redisClusterAsyncContext *acc,
                                        redisConnectCallback *fn);
int redisClusterAsyncSetDisonnectCallback(redisClusterAsyncContext *acc,
                                          redisConnectCallback *fn);
```

The callback functions should have the following prototype,
aliased to `redisConnectCallback`:

```c
void(const redisAsyncContext *ac, int status);
```

On a connection attempt, the `status` argument is set to `REDIS_OK`
when the connection was successful.
The file description of the connection socket can be retrieved
from a redisAsyncContext as `ac->c->fd`.
On a disconnect, the `status` argument is set to `REDIS_OK`
when disconnection was initiated by the user,
or `REDIS_ERR` when the disconnection was caused by an error.
When it is `REDIS_ERR`, the `err` field in the context can be accessed
to find out the cause of the error.

You don't need to reconnect in the disconnect callback.
Hiredis-cluster will reconnect by itself when the next command for this Redis node is handled.

Setting the connect and disconnect callbacks can only be done once per context.
For subsequent calls it will return `REDIS_ERR`.

### Sending commands and their callbacks

In an asynchronous cluster context, commands are automatically pipelined due to the nature of an event loop.
Therefore, unlike the synchronous API, there is only a single way to send commands.
Because commands are sent to Redis Cluster asynchronously, issuing a command requires a callback function
that is called when the reply is received. Reply callbacks should have the following prototype:
```c
void(redisClusterAsyncContext *acc, void *reply, void *privdata);
```
The `privdata` argument can be used to carry arbitrary data to the callback from the point where
the command is initially queued for execution.

The most commonly used functions to issue commands in an asynchronous context are:
```c
int redisClusterAsyncCommand(redisClusterAsyncContext *acc,
                             redisClusterCallbackFn *fn,
                             void *privdata, const char *format, ...);
int redisClusterAsyncCommandArgv(redisClusterAsyncContext *acc,
                                 redisClusterCallbackFn *fn, void *privdata,
                                 int argc, const char **argv,
                                 const size_t *argvlen);
int redisClusterAsyncFormattedCommand(redisClusterAsyncContext *acc,
                                      redisClusterCallbackFn *fn,
                                      void *privdata, char *cmd, int len);
```
These functions works like their blocking counterparts. The return value is `REDIS_OK` when the command
was successfully added to the output buffer and `REDIS_ERR` otherwise. When the connection is being
disconnected per user-request, no new commands may be added to the output buffer and `REDIS_ERR` is
returned.

If the reply for a command with a `NULL` callback is read, it is immediately freed. When the callback
for a command is non-`NULL`, the memory is freed immediately following the callback: the reply is only
valid for the duration of the callback.

All pending callbacks are called with a `NULL` reply when the context encountered an error.

### Sending commands to a specific node

When there is a need to send commands to a specific node, the following low-level API can be used.

```c
int redisClusterAsyncCommandToNode(redisClusterAsyncContext *acc,
                                   redisClusterNode *node,
                                   redisClusterCallbackFn *fn, void *privdata,
                                   const char *format, ...);
int redisClusterAsyncCommandArgvToNode(redisClusterAsyncContext *acc,
                                       redisClusterNode *node,
                                       redisClusterCallbackFn *fn,
                                       void *privdata, int argc,
                                       const char **argv,
                                       const size_t *argvlen);
int redisClusterAsyncFormattedCommandToNode(redisClusterAsyncContext *acc,
                                            redisClusterNode *node,
                                            redisClusterCallbackFn *fn,
                                            void *privdata, char *cmd, int len);
```

These functions will only attempt to send the command to a specific node and will not perform redirects or retries,
but communication errors will trigger a slotmap update just like the commonly used API.

### Disconnecting

Asynchronous cluster connections can be terminated using:
```c
void redisClusterAsyncDisconnect(redisClusterAsyncContext *acc);
```
When this function is called, connections are **not** immediately terminated. Instead, new
commands are no longer accepted and connections are only terminated when all pending commands
have been written to a socket, their respective replies have been read and their respective
callbacks have been executed. After this, the disconnection callback is executed with the
`REDIS_OK` status and the context object is freed.

### Using event library *X*

There are a few hooks that need to be set on the cluster context object after it is created.
See the `adapters/` directory for bindings to *libevent* and a range of other event libraries.

## Other details

### Cluster node iterator

A `redisClusterNodeIterator` can be used to iterate on all known master nodes in a cluster context.
First it needs to be initiated using `redisClusterInitNodeIterator()` and then you can repeatedly
call `redisClusterNodeNext()` to get the next node from the iterator.

```c
void redisClusterInitNodeIterator(redisClusterNodeIterator *iter,
                                  redisClusterContext *cc);
redisClusterNode *redisClusterNodeNext(redisClusterNodeIterator *iter);
```

The iterator will handle changes due to slotmap updates by restarting the iteration, but on the new
set of master nodes. There is no bookkeeping for already iterated nodes when a restart is triggered,
which means that a node can be iterated over more than once depending on when the slotmap update happened
and the change of cluster nodes.

Note that when `redisClusterCommandToNode` is called, a slotmap update can
happen if it has been scheduled by the previous command, for example if the
previous call to `redisClusterCommandToNode` timed out or the node wasn't
reachable.

To detect when the slotmap has been updated, you can check if the iterator's
slotmap version (`iter.route_version`) is equal to the current cluster context's
slotmap version (`cc->route_version`). If it isn't, it means that the slotmap
has been updated and the iterator will restart itself at the next call to
`redisClusterNodeNext`.

Another way to detect that the slotmap has been updated is to [register an event
callback](#events-per-cluster-context) and look for the event
`HIRCLUSTER_EVENT_SLOTMAP_UPDATED`.

### Random number generator

This library uses [random()](https://linux.die.net/man/3/random) while selecting
a node used for requesting the cluster topology (slotmap). A user should seed
the random number generator using [srandom()](https://linux.die.net/man/3/srandom)
to get less predictability in the node selection.

### Allocator injection

Hiredis-cluster uses hiredis allocation structure with configurable allocation and deallocation functions. By default they just point to libc (`malloc`, `calloc`, `realloc`, etc).

#### Overriding

If you have your own allocator or if you expect an abort in out-of-memory cases,
you can configure the used functions in the following way:

```c
hiredisAllocFuncs myfuncs = {
    .mallocFn = my_malloc,
    .callocFn = my_calloc,
    .reallocFn = my_realloc,
    .strdupFn = my_strdup,
    .freeFn = my_free,
};

// Override allocators (function returns current allocators if needed)
hiredisAllocFuncs orig = hiredisSetAllocators(&myfuncs);
```

To reset the allocators to their default libc functions simply call:

```c
hiredisResetAllocators();
```

### 0.11.0 - Sep 15, 2023

* Add event callback for events like 'slotmap updated'.
* Add connect callback for the sync API.
* Add connect function in the async API for fully asynchronous startup.
* Update the slotmap asynchronously in the async API.
* Follow MOVED redirect and update slot mapping concurrently.
* Update slotmap on error.
  When connect failed, update slotmap instead of sending command to random node.
  When command fails (timeout, etc.) schedule slotmap update for next command.
* Update slotmap when redisClusterCommandToNode() fails.
* Correct parsing of an IPv6 address in an ASK redirect.
* Correct handling of XREAD and XREADGROUP.
* Rename of some types and functions.
  (Old names are still defined by default for backward compability.)
* Update hiredis to v1.2.0 when the CMake build handles the download.
* Build improvements.

### 0.10.0 - Feb 02, 2023

* More commands are supported.
* New logic for finding the key of each command.
* Build improvements.

### 0.9.0 - Dec 22, 2022

* Fixed a crash in the asynchronous API triggered by timed out commands.
* Fixed a crash when using "CLUSTER NODES" on a non-ready cluster.
* Fixed a crash when sending commands from a failure-reply callback.
* Corrected and enabled connection timeout in the asynchronous API.
* Corrected the handling of multiple ASK-redirect reply callbacks.
* Updated hiredis to v1.1.0 when the CMake build handles the download.
* Removed the unused cluster_slots array in the cluster context.
* Updates to resolve build issues on Windows platforms.

### 0.8.1 - Aug 31, 2022

* Fixed crash and use-after-free in the asynchronous API.
* Use identical warning flags in CMake and Makefile.
* Corrected CROSSSLOT errors to not to be retried.

### 0.8.0 - Jun 15, 2022

* Basic Redis 7.0 support.
* SSL/TLS handling in separate library.
* Command timeout corrections.
* Builds on Windows and macOS.

### 0.7.0 - Sep 22, 2021

* Added support for stream commands in regular API.
* Added support for authentication using AUTH with username.
* Added adapters for event libraries libuv, libev and GLib.
* Improved memory efficiency.
* Renamed API function `redisClusterSetOptionMaxRedirect()`
  to `redisClusterSetOptionMaxRetry()`.

### 0.6.0 - Feb 09, 2021

* Minimum required version of CMake changed to 3.11 (from 3.14)
* Re-added the Makefile for symmetry with hiredis, which also enables
  support for statically-linked libraries.
* Improved examples
* Corrected crashes and leaks in OOM scenarios
* New API for sending commands to specific node
* New API for node iteration, can be used for sending commands
  to some or all nodes.

### 0.5.0 - Dec 07, 2020

* Renamed to [hiredis-cluster](https://github.com/Nordix/hiredis-cluster)
* The C library `hiredis` is an external dependency rather than a builtin part
  of the cluster client, meaning that `hiredis` v1.0.0 or later can be used.
* Support for SSL/TLS introduced in Redis 6
* Support for IPv6
* Support authentication using AUTH
* Handle variable number of keys in command EXISTS
* Improved CMake build
* Code style guide (using clang-format)
* Improved testing
* Memory leak corrections and allocation failure handling

### 0.4.0 - Jan 24, 2019

* Updated underlying hiredis version to 0.14.0
* Added CMake files to enable Windows and Mac builds
* Fixed bug due to CLUSTER NODES reply format change

https://github.com/heronr/hiredis-vip

### 0.3.0 - Dec 07, 2016

* Support redisClustervCommand, redisClustervAppendCommand and redisClustervAsyncCommand api. (deep011)
* Add flags HIRCLUSTER_FLAG_ADD_OPENSLOT and HIRCLUSTER_FLAG_ROUTE_USE_SLOTS. (deep011)
* Support redisClusterCommandArgv related api. (deep011)
* Fix some serious bugs. (deep011)

https://github.com/vipshop/hiredis-vip

### 0.2.1 - Nov 24, 2015

This release support redis cluster api.

* Add hiredis 0.3.1. (deep011)
* Support cluster synchronous API. (deep011)
* Support multi-key command(mget/mset/del) for redis cluster. (deep011)
* Support cluster pipelining. (deep011)
* Support cluster asynchronous API. (deep011)

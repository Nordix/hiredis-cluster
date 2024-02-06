/* This file was generated using gencommands.py */

/* clang-format off */
COMMAND(ACL_CAT, "ACL", "CAT", -2, NONE, 0)
COMMAND(ACL_DELUSER, "ACL", "DELUSER", -3, NONE, 0)
COMMAND(ACL_DRYRUN, "ACL", "DRYRUN", -4, NONE, 0)
COMMAND(ACL_GENPASS, "ACL", "GENPASS", -2, NONE, 0)
COMMAND(ACL_GETUSER, "ACL", "GETUSER", 3, NONE, 0)
COMMAND(ACL_HELP, "ACL", "HELP", 2, NONE, 0)
COMMAND(ACL_LIST, "ACL", "LIST", 2, NONE, 0)
COMMAND(ACL_LOAD, "ACL", "LOAD", 2, NONE, 0)
COMMAND(ACL_LOG, "ACL", "LOG", -2, NONE, 0)
COMMAND(ACL_SAVE, "ACL", "SAVE", 2, NONE, 0)
COMMAND(ACL_SETUSER, "ACL", "SETUSER", -3, NONE, 0)
COMMAND(ACL_USERS, "ACL", "USERS", 2, NONE, 0)
COMMAND(ACL_WHOAMI, "ACL", "WHOAMI", 2, NONE, 0)
COMMAND(APPEND, "APPEND", NULL, 3, INDEX, 1)
COMMAND(ASKING, "ASKING", NULL, 1, NONE, 0)
COMMAND(AUTH, "AUTH", NULL, -2, NONE, 0)
COMMAND(BGREWRITEAOF, "BGREWRITEAOF", NULL, 1, NONE, 0)
COMMAND(BGSAVE, "BGSAVE", NULL, -1, NONE, 0)
COMMAND(BITCOUNT, "BITCOUNT", NULL, -2, INDEX, 1)
COMMAND(BITFIELD, "BITFIELD", NULL, -2, INDEX, 1)
COMMAND(BITFIELD_RO, "BITFIELD_RO", NULL, -2, INDEX, 1)
COMMAND(BITOP, "BITOP", NULL, -4, INDEX, 2)
COMMAND(BITPOS, "BITPOS", NULL, -3, INDEX, 1)
COMMAND(BLMOVE, "BLMOVE", NULL, 6, INDEX, 1)
COMMAND(BLMPOP, "BLMPOP", NULL, -5, KEYNUM, 2)
COMMAND(BLPOP, "BLPOP", NULL, -3, INDEX, 1)
COMMAND(BRPOP, "BRPOP", NULL, -3, INDEX, 1)
COMMAND(BRPOPLPUSH, "BRPOPLPUSH", NULL, 4, INDEX, 1)
COMMAND(BZMPOP, "BZMPOP", NULL, -5, KEYNUM, 2)
COMMAND(BZPOPMAX, "BZPOPMAX", NULL, -3, INDEX, 1)
COMMAND(BZPOPMIN, "BZPOPMIN", NULL, -3, INDEX, 1)
COMMAND(CLIENT_CACHING, "CLIENT", "CACHING", 3, NONE, 0)
COMMAND(CLIENT_GETNAME, "CLIENT", "GETNAME", 2, NONE, 0)
COMMAND(CLIENT_GETREDIR, "CLIENT", "GETREDIR", 2, NONE, 0)
COMMAND(CLIENT_HELP, "CLIENT", "HELP", 2, NONE, 0)
COMMAND(CLIENT_ID, "CLIENT", "ID", 2, NONE, 0)
COMMAND(CLIENT_INFO, "CLIENT", "INFO", 2, NONE, 0)
COMMAND(CLIENT_KILL, "CLIENT", "KILL", -3, NONE, 0)
COMMAND(CLIENT_LIST, "CLIENT", "LIST", -2, NONE, 0)
COMMAND(CLIENT_NO_EVICT, "CLIENT", "NO-EVICT", 3, NONE, 0)
COMMAND(CLIENT_PAUSE, "CLIENT", "PAUSE", -3, NONE, 0)
COMMAND(CLIENT_REPLY, "CLIENT", "REPLY", 3, NONE, 0)
COMMAND(CLIENT_SETNAME, "CLIENT", "SETNAME", 3, NONE, 0)
COMMAND(CLIENT_TRACKING, "CLIENT", "TRACKING", -3, NONE, 0)
COMMAND(CLIENT_TRACKINGINFO, "CLIENT", "TRACKINGINFO", 2, NONE, 0)
COMMAND(CLIENT_UNBLOCK, "CLIENT", "UNBLOCK", -3, NONE, 0)
COMMAND(CLIENT_UNPAUSE, "CLIENT", "UNPAUSE", 2, NONE, 0)
COMMAND(CLUSTER_ADDSLOTS, "CLUSTER", "ADDSLOTS", -3, NONE, 0)
COMMAND(CLUSTER_ADDSLOTSRANGE, "CLUSTER", "ADDSLOTSRANGE", -4, NONE, 0)
COMMAND(CLUSTER_BUMPEPOCH, "CLUSTER", "BUMPEPOCH", 2, NONE, 0)
COMMAND(CLUSTER_COUNT_FAILURE_REPORTS, "CLUSTER", "COUNT-FAILURE-REPORTS", 3, NONE, 0)
COMMAND(CLUSTER_COUNTKEYSINSLOT, "CLUSTER", "COUNTKEYSINSLOT", 3, NONE, 0)
COMMAND(CLUSTER_DELSLOTS, "CLUSTER", "DELSLOTS", -3, NONE, 0)
COMMAND(CLUSTER_DELSLOTSRANGE, "CLUSTER", "DELSLOTSRANGE", -4, NONE, 0)
COMMAND(CLUSTER_FAILOVER, "CLUSTER", "FAILOVER", -2, NONE, 0)
COMMAND(CLUSTER_FLUSHSLOTS, "CLUSTER", "FLUSHSLOTS", 2, NONE, 0)
COMMAND(CLUSTER_FORGET, "CLUSTER", "FORGET", 3, NONE, 0)
COMMAND(CLUSTER_GETKEYSINSLOT, "CLUSTER", "GETKEYSINSLOT", 4, NONE, 0)
COMMAND(CLUSTER_HELP, "CLUSTER", "HELP", 2, NONE, 0)
COMMAND(CLUSTER_INFO, "CLUSTER", "INFO", 2, NONE, 0)
COMMAND(CLUSTER_KEYSLOT, "CLUSTER", "KEYSLOT", 3, NONE, 0)
COMMAND(CLUSTER_LINKS, "CLUSTER", "LINKS", 2, NONE, 0)
COMMAND(CLUSTER_MEET, "CLUSTER", "MEET", -4, NONE, 0)
COMMAND(CLUSTER_MYID, "CLUSTER", "MYID", 2, NONE, 0)
COMMAND(CLUSTER_MYSHARDID, "CLUSTER", "MYSHARDID", 2, NONE, 0)
COMMAND(CLUSTER_NODES, "CLUSTER", "NODES", 2, NONE, 0)
COMMAND(CLUSTER_REPLICAS, "CLUSTER", "REPLICAS", 3, NONE, 0)
COMMAND(CLUSTER_REPLICATE, "CLUSTER", "REPLICATE", 3, NONE, 0)
COMMAND(CLUSTER_RESET, "CLUSTER", "RESET", -2, NONE, 0)
COMMAND(CLUSTER_SAVECONFIG, "CLUSTER", "SAVECONFIG", 2, NONE, 0)
COMMAND(CLUSTER_SET_CONFIG_EPOCH, "CLUSTER", "SET-CONFIG-EPOCH", 3, NONE, 0)
COMMAND(CLUSTER_SETSLOT, "CLUSTER", "SETSLOT", -4, NONE, 0)
COMMAND(CLUSTER_SHARDS, "CLUSTER", "SHARDS", 2, NONE, 0)
COMMAND(CLUSTER_SLAVES, "CLUSTER", "SLAVES", 3, NONE, 0)
COMMAND(CLUSTER_SLOTS, "CLUSTER", "SLOTS", 2, NONE, 0)
COMMAND(COMMAND_COUNT, "COMMAND", "COUNT", 2, NONE, 0)
COMMAND(COMMAND_DOCS, "COMMAND", "DOCS", -2, NONE, 0)
COMMAND(COMMAND_GETKEYS, "COMMAND", "GETKEYS", -3, NONE, 0)
COMMAND(COMMAND_GETKEYSANDFLAGS, "COMMAND", "GETKEYSANDFLAGS", -3, NONE, 0)
COMMAND(COMMAND_HELP, "COMMAND", "HELP", 2, NONE, 0)
COMMAND(COMMAND_INFO, "COMMAND", "INFO", -2, NONE, 0)
COMMAND(COMMAND_LIST, "COMMAND", "LIST", -2, NONE, 0)
COMMAND(CONFIG_GET, "CONFIG", "GET", -3, NONE, 0)
COMMAND(CONFIG_HELP, "CONFIG", "HELP", 2, NONE, 0)
COMMAND(CONFIG_RESETSTAT, "CONFIG", "RESETSTAT", 2, NONE, 0)
COMMAND(CONFIG_REWRITE, "CONFIG", "REWRITE", 2, NONE, 0)
COMMAND(CONFIG_SET, "CONFIG", "SET", -4, NONE, 0)
COMMAND(COPY, "COPY", NULL, -3, INDEX, 1)
COMMAND(DBSIZE, "DBSIZE", NULL, 1, NONE, 0)
COMMAND(DEBUG, "DEBUG", NULL, -2, NONE, 0)
COMMAND(DECR, "DECR", NULL, 2, INDEX, 1)
COMMAND(DECRBY, "DECRBY", NULL, 3, INDEX, 1)
COMMAND(DEL, "DEL", NULL, -2, INDEX, 1)
COMMAND(DISCARD, "DISCARD", NULL, 1, NONE, 0)
COMMAND(DUMP, "DUMP", NULL, 2, INDEX, 1)
COMMAND(ECHO, "ECHO", NULL, 2, NONE, 0)
COMMAND(EVAL, "EVAL", NULL, -3, KEYNUM, 2)
COMMAND(EVALSHA, "EVALSHA", NULL, -3, KEYNUM, 2)
COMMAND(EVALSHA_RO, "EVALSHA_RO", NULL, -3, KEYNUM, 2)
COMMAND(EVAL_RO, "EVAL_RO", NULL, -3, KEYNUM, 2)
COMMAND(EXEC, "EXEC", NULL, 1, NONE, 0)
COMMAND(EXISTS, "EXISTS", NULL, -2, INDEX, 1)
COMMAND(EXPIRE, "EXPIRE", NULL, -3, INDEX, 1)
COMMAND(EXPIREAT, "EXPIREAT", NULL, -3, INDEX, 1)
COMMAND(EXPIRETIME, "EXPIRETIME", NULL, 2, INDEX, 1)
COMMAND(FAILOVER, "FAILOVER", NULL, -1, NONE, 0)
COMMAND(FCALL, "FCALL", NULL, -3, KEYNUM, 2)
COMMAND(FCALL_RO, "FCALL_RO", NULL, -3, KEYNUM, 2)
COMMAND(FLUSHALL, "FLUSHALL", NULL, -1, NONE, 0)
COMMAND(FLUSHDB, "FLUSHDB", NULL, -1, NONE, 0)
COMMAND(FUNCTION_DELETE, "FUNCTION", "DELETE", 3, NONE, 0)
COMMAND(FUNCTION_DUMP, "FUNCTION", "DUMP", 2, NONE, 0)
COMMAND(FUNCTION_FLUSH, "FUNCTION", "FLUSH", -2, NONE, 0)
COMMAND(FUNCTION_HELP, "FUNCTION", "HELP", 2, NONE, 0)
COMMAND(FUNCTION_KILL, "FUNCTION", "KILL", 2, NONE, 0)
COMMAND(FUNCTION_LIST, "FUNCTION", "LIST", -2, NONE, 0)
COMMAND(FUNCTION_LOAD, "FUNCTION", "LOAD", -3, NONE, 0)
COMMAND(FUNCTION_RESTORE, "FUNCTION", "RESTORE", -3, NONE, 0)
COMMAND(FUNCTION_STATS, "FUNCTION", "STATS", 2, NONE, 0)
COMMAND(GEOADD, "GEOADD", NULL, -5, INDEX, 1)
COMMAND(GEODIST, "GEODIST", NULL, -4, INDEX, 1)
COMMAND(GEOHASH, "GEOHASH", NULL, -2, INDEX, 1)
COMMAND(GEOPOS, "GEOPOS", NULL, -2, INDEX, 1)
COMMAND(GEORADIUS, "GEORADIUS", NULL, -6, INDEX, 1)
COMMAND(GEORADIUSBYMEMBER, "GEORADIUSBYMEMBER", NULL, -5, INDEX, 1)
COMMAND(GEORADIUSBYMEMBER_RO, "GEORADIUSBYMEMBER_RO", NULL, -5, INDEX, 1)
COMMAND(GEORADIUS_RO, "GEORADIUS_RO", NULL, -6, INDEX, 1)
COMMAND(GEOSEARCH, "GEOSEARCH", NULL, -7, INDEX, 1)
COMMAND(GEOSEARCHSTORE, "GEOSEARCHSTORE", NULL, -8, INDEX, 1)
COMMAND(GET, "GET", NULL, 2, INDEX, 1)
COMMAND(GETBIT, "GETBIT", NULL, 3, INDEX, 1)
COMMAND(GETDEL, "GETDEL", NULL, 2, INDEX, 1)
COMMAND(GETEX, "GETEX", NULL, -2, INDEX, 1)
COMMAND(GETRANGE, "GETRANGE", NULL, 4, INDEX, 1)
COMMAND(GETSET, "GETSET", NULL, 3, INDEX, 1)
COMMAND(HDEL, "HDEL", NULL, -3, INDEX, 1)
COMMAND(HELLO, "HELLO", NULL, -1, NONE, 0)
COMMAND(HEXISTS, "HEXISTS", NULL, 3, INDEX, 1)
COMMAND(HGET, "HGET", NULL, 3, INDEX, 1)
COMMAND(HGETALL, "HGETALL", NULL, 2, INDEX, 1)
COMMAND(HINCRBY, "HINCRBY", NULL, 4, INDEX, 1)
COMMAND(HINCRBYFLOAT, "HINCRBYFLOAT", NULL, 4, INDEX, 1)
COMMAND(HKEYS, "HKEYS", NULL, 2, INDEX, 1)
COMMAND(HLEN, "HLEN", NULL, 2, INDEX, 1)
COMMAND(HMGET, "HMGET", NULL, -3, INDEX, 1)
COMMAND(HMSET, "HMSET", NULL, -4, INDEX, 1)
COMMAND(HRANDFIELD, "HRANDFIELD", NULL, -2, INDEX, 1)
COMMAND(HSCAN, "HSCAN", NULL, -3, INDEX, 1)
COMMAND(HSET, "HSET", NULL, -4, INDEX, 1)
COMMAND(HSETNX, "HSETNX", NULL, 4, INDEX, 1)
COMMAND(HSTRLEN, "HSTRLEN", NULL, 3, INDEX, 1)
COMMAND(HVALS, "HVALS", NULL, 2, INDEX, 1)
COMMAND(INCR, "INCR", NULL, 2, INDEX, 1)
COMMAND(INCRBY, "INCRBY", NULL, 3, INDEX, 1)
COMMAND(INCRBYFLOAT, "INCRBYFLOAT", NULL, 3, INDEX, 1)
COMMAND(INFO, "INFO", NULL, -1, NONE, 0)
COMMAND(KEYS, "KEYS", NULL, 2, NONE, 0)
COMMAND(LASTSAVE, "LASTSAVE", NULL, 1, NONE, 0)
COMMAND(LATENCY_DOCTOR, "LATENCY", "DOCTOR", 2, NONE, 0)
COMMAND(LATENCY_GRAPH, "LATENCY", "GRAPH", 3, NONE, 0)
COMMAND(LATENCY_HELP, "LATENCY", "HELP", 2, NONE, 0)
COMMAND(LATENCY_HISTOGRAM, "LATENCY", "HISTOGRAM", -2, NONE, 0)
COMMAND(LATENCY_HISTORY, "LATENCY", "HISTORY", 3, NONE, 0)
COMMAND(LATENCY_LATEST, "LATENCY", "LATEST", 2, NONE, 0)
COMMAND(LATENCY_RESET, "LATENCY", "RESET", -2, NONE, 0)
COMMAND(LCS, "LCS", NULL, -3, INDEX, 1)
COMMAND(LINDEX, "LINDEX", NULL, 3, INDEX, 1)
COMMAND(LINSERT, "LINSERT", NULL, 5, INDEX, 1)
COMMAND(LLEN, "LLEN", NULL, 2, INDEX, 1)
COMMAND(LMOVE, "LMOVE", NULL, 5, INDEX, 1)
COMMAND(LMPOP, "LMPOP", NULL, -4, KEYNUM, 1)
COMMAND(LOLWUT, "LOLWUT", NULL, -1, NONE, 0)
COMMAND(LPOP, "LPOP", NULL, -2, INDEX, 1)
COMMAND(LPOS, "LPOS", NULL, -3, INDEX, 1)
COMMAND(LPUSH, "LPUSH", NULL, -3, INDEX, 1)
COMMAND(LPUSHX, "LPUSHX", NULL, -3, INDEX, 1)
COMMAND(LRANGE, "LRANGE", NULL, 4, INDEX, 1)
COMMAND(LREM, "LREM", NULL, 4, INDEX, 1)
COMMAND(LSET, "LSET", NULL, 4, INDEX, 1)
COMMAND(LTRIM, "LTRIM", NULL, 4, INDEX, 1)
COMMAND(MEMORY_DOCTOR, "MEMORY", "DOCTOR", 2, NONE, 0)
COMMAND(MEMORY_HELP, "MEMORY", "HELP", 2, NONE, 0)
COMMAND(MEMORY_MALLOC_STATS, "MEMORY", "MALLOC-STATS", 2, NONE, 0)
COMMAND(MEMORY_PURGE, "MEMORY", "PURGE", 2, NONE, 0)
COMMAND(MEMORY_STATS, "MEMORY", "STATS", 2, NONE, 0)
COMMAND(MEMORY_USAGE, "MEMORY", "USAGE", -3, INDEX, 2)
COMMAND(MGET, "MGET", NULL, -2, INDEX, 1)
COMMAND(MIGRATE, "MIGRATE", NULL, -6, INDEX, 3)
COMMAND(MODULE_HELP, "MODULE", "HELP", 2, NONE, 0)
COMMAND(MODULE_LIST, "MODULE", "LIST", 2, NONE, 0)
COMMAND(MODULE_LOAD, "MODULE", "LOAD", -3, NONE, 0)
COMMAND(MODULE_LOADEX, "MODULE", "LOADEX", -3, NONE, 0)
COMMAND(MODULE_UNLOAD, "MODULE", "UNLOAD", 3, NONE, 0)
COMMAND(MONITOR, "MONITOR", NULL, 1, NONE, 0)
COMMAND(MOVE, "MOVE", NULL, 3, INDEX, 1)
COMMAND(MSET, "MSET", NULL, -3, INDEX, 1)
COMMAND(MSETNX, "MSETNX", NULL, -3, INDEX, 1)
COMMAND(MULTI, "MULTI", NULL, 1, NONE, 0)
COMMAND(OBJECT_ENCODING, "OBJECT", "ENCODING", 3, INDEX, 2)
COMMAND(OBJECT_FREQ, "OBJECT", "FREQ", 3, INDEX, 2)
COMMAND(OBJECT_HELP, "OBJECT", "HELP", 2, NONE, 0)
COMMAND(OBJECT_IDLETIME, "OBJECT", "IDLETIME", 3, INDEX, 2)
COMMAND(OBJECT_REFCOUNT, "OBJECT", "REFCOUNT", 3, INDEX, 2)
COMMAND(PERSIST, "PERSIST", NULL, 2, INDEX, 1)
COMMAND(PEXPIRE, "PEXPIRE", NULL, -3, INDEX, 1)
COMMAND(PEXPIREAT, "PEXPIREAT", NULL, -3, INDEX, 1)
COMMAND(PEXPIRETIME, "PEXPIRETIME", NULL, 2, INDEX, 1)
COMMAND(PFADD, "PFADD", NULL, -2, INDEX, 1)
COMMAND(PFCOUNT, "PFCOUNT", NULL, -2, INDEX, 1)
COMMAND(PFDEBUG, "PFDEBUG", NULL, 3, INDEX, 2)
COMMAND(PFMERGE, "PFMERGE", NULL, -2, INDEX, 1)
COMMAND(PFSELFTEST, "PFSELFTEST", NULL, 1, NONE, 0)
COMMAND(PING, "PING", NULL, -1, NONE, 0)
COMMAND(PSETEX, "PSETEX", NULL, 4, INDEX, 1)
COMMAND(PSUBSCRIBE, "PSUBSCRIBE", NULL, -2, NONE, 0)
COMMAND(PSYNC, "PSYNC", NULL, -3, NONE, 0)
COMMAND(PTTL, "PTTL", NULL, 2, INDEX, 1)
COMMAND(PUBLISH, "PUBLISH", NULL, 3, NONE, 0)
COMMAND(PUBSUB_CHANNELS, "PUBSUB", "CHANNELS", -2, NONE, 0)
COMMAND(PUBSUB_HELP, "PUBSUB", "HELP", 2, NONE, 0)
COMMAND(PUBSUB_NUMPAT, "PUBSUB", "NUMPAT", 2, NONE, 0)
COMMAND(PUBSUB_NUMSUB, "PUBSUB", "NUMSUB", -2, NONE, 0)
COMMAND(PUBSUB_SHARDCHANNELS, "PUBSUB", "SHARDCHANNELS", -2, NONE, 0)
COMMAND(PUBSUB_SHARDNUMSUB, "PUBSUB", "SHARDNUMSUB", -2, NONE, 0)
COMMAND(PUNSUBSCRIBE, "PUNSUBSCRIBE", NULL, -1, NONE, 0)
COMMAND(QUIT, "QUIT", NULL, -1, NONE, 0)
COMMAND(RANDOMKEY, "RANDOMKEY", NULL, 1, NONE, 0)
COMMAND(READONLY, "READONLY", NULL, 1, NONE, 0)
COMMAND(READWRITE, "READWRITE", NULL, 1, NONE, 0)
COMMAND(RENAME, "RENAME", NULL, 3, INDEX, 1)
COMMAND(RENAMENX, "RENAMENX", NULL, 3, INDEX, 1)
COMMAND(REPLCONF, "REPLCONF", NULL, -1, NONE, 0)
COMMAND(REPLICAOF, "REPLICAOF", NULL, 3, NONE, 0)
COMMAND(RESET, "RESET", NULL, 1, NONE, 0)
COMMAND(RESTORE, "RESTORE", NULL, -4, INDEX, 1)
COMMAND(RESTORE_ASKING, "RESTORE-ASKING", NULL, -4, INDEX, 1)
COMMAND(ROLE, "ROLE", NULL, 1, NONE, 0)
COMMAND(RPOP, "RPOP", NULL, -2, INDEX, 1)
COMMAND(RPOPLPUSH, "RPOPLPUSH", NULL, 3, INDEX, 1)
COMMAND(RPUSH, "RPUSH", NULL, -3, INDEX, 1)
COMMAND(RPUSHX, "RPUSHX", NULL, -3, INDEX, 1)
COMMAND(SADD, "SADD", NULL, -3, INDEX, 1)
COMMAND(SAVE, "SAVE", NULL, 1, NONE, 0)
COMMAND(SCAN, "SCAN", NULL, -2, NONE, 0)
COMMAND(SCARD, "SCARD", NULL, 2, INDEX, 1)
COMMAND(SCRIPT_DEBUG, "SCRIPT", "DEBUG", 3, NONE, 0)
COMMAND(SCRIPT_EXISTS, "SCRIPT", "EXISTS", -3, NONE, 0)
COMMAND(SCRIPT_FLUSH, "SCRIPT", "FLUSH", -2, NONE, 0)
COMMAND(SCRIPT_HELP, "SCRIPT", "HELP", 2, NONE, 0)
COMMAND(SCRIPT_KILL, "SCRIPT", "KILL", 2, NONE, 0)
COMMAND(SCRIPT_LOAD, "SCRIPT", "LOAD", 3, NONE, 0)
COMMAND(SDIFF, "SDIFF", NULL, -2, INDEX, 1)
COMMAND(SDIFFSTORE, "SDIFFSTORE", NULL, -3, INDEX, 1)
COMMAND(SELECT, "SELECT", NULL, 2, NONE, 0)
COMMAND(SENTINEL_CKQUORUM, "SENTINEL", "CKQUORUM", 3, NONE, 0)
COMMAND(SENTINEL_CONFIG, "SENTINEL", "CONFIG", -3, NONE, 0)
COMMAND(SENTINEL_DEBUG, "SENTINEL", "DEBUG", -2, NONE, 0)
COMMAND(SENTINEL_FAILOVER, "SENTINEL", "FAILOVER", 3, NONE, 0)
COMMAND(SENTINEL_FLUSHCONFIG, "SENTINEL", "FLUSHCONFIG", 2, NONE, 0)
COMMAND(SENTINEL_GET_MASTER_ADDR_BY_NAME, "SENTINEL", "GET-MASTER-ADDR-BY-NAME", 3, NONE, 0)
COMMAND(SENTINEL_HELP, "SENTINEL", "HELP", 2, NONE, 0)
COMMAND(SENTINEL_INFO_CACHE, "SENTINEL", "INFO-CACHE", -3, NONE, 0)
COMMAND(SENTINEL_IS_MASTER_DOWN_BY_ADDR, "SENTINEL", "IS-MASTER-DOWN-BY-ADDR", 6, NONE, 0)
COMMAND(SENTINEL_MASTER, "SENTINEL", "MASTER", 3, NONE, 0)
COMMAND(SENTINEL_MASTERS, "SENTINEL", "MASTERS", 2, NONE, 0)
COMMAND(SENTINEL_MONITOR, "SENTINEL", "MONITOR", 6, NONE, 0)
COMMAND(SENTINEL_MYID, "SENTINEL", "MYID", 2, NONE, 0)
COMMAND(SENTINEL_PENDING_SCRIPTS, "SENTINEL", "PENDING-SCRIPTS", 2, NONE, 0)
COMMAND(SENTINEL_REMOVE, "SENTINEL", "REMOVE", 3, NONE, 0)
COMMAND(SENTINEL_REPLICAS, "SENTINEL", "REPLICAS", 3, NONE, 0)
COMMAND(SENTINEL_RESET, "SENTINEL", "RESET", 3, NONE, 0)
COMMAND(SENTINEL_SENTINELS, "SENTINEL", "SENTINELS", 3, NONE, 0)
COMMAND(SENTINEL_SET, "SENTINEL", "SET", -5, NONE, 0)
COMMAND(SENTINEL_SIMULATE_FAILURE, "SENTINEL", "SIMULATE-FAILURE", -3, NONE, 0)
COMMAND(SENTINEL_SLAVES, "SENTINEL", "SLAVES", 3, NONE, 0)
COMMAND(SET, "SET", NULL, -3, INDEX, 1)
COMMAND(SETBIT, "SETBIT", NULL, 4, INDEX, 1)
COMMAND(SETEX, "SETEX", NULL, 4, INDEX, 1)
COMMAND(SETNX, "SETNX", NULL, 3, INDEX, 1)
COMMAND(SETRANGE, "SETRANGE", NULL, 4, INDEX, 1)
COMMAND(SHUTDOWN, "SHUTDOWN", NULL, -1, NONE, 0)
COMMAND(SINTER, "SINTER", NULL, -2, INDEX, 1)
COMMAND(SINTERCARD, "SINTERCARD", NULL, -3, KEYNUM, 1)
COMMAND(SINTERSTORE, "SINTERSTORE", NULL, -3, INDEX, 1)
COMMAND(SISMEMBER, "SISMEMBER", NULL, 3, INDEX, 1)
COMMAND(SLAVEOF, "SLAVEOF", NULL, 3, NONE, 0)
COMMAND(SLOWLOG_GET, "SLOWLOG", "GET", -2, NONE, 0)
COMMAND(SLOWLOG_HELP, "SLOWLOG", "HELP", 2, NONE, 0)
COMMAND(SLOWLOG_LEN, "SLOWLOG", "LEN", 2, NONE, 0)
COMMAND(SLOWLOG_RESET, "SLOWLOG", "RESET", 2, NONE, 0)
COMMAND(SMEMBERS, "SMEMBERS", NULL, 2, INDEX, 1)
COMMAND(SMISMEMBER, "SMISMEMBER", NULL, -3, INDEX, 1)
COMMAND(SMOVE, "SMOVE", NULL, 4, INDEX, 1)
COMMAND(SORT, "SORT", NULL, -2, INDEX, 1)
COMMAND(SORT_RO, "SORT_RO", NULL, -2, INDEX, 1)
COMMAND(SPOP, "SPOP", NULL, -2, INDEX, 1)
COMMAND(SPUBLISH, "SPUBLISH", NULL, 3, INDEX, 1)
COMMAND(SRANDMEMBER, "SRANDMEMBER", NULL, -2, INDEX, 1)
COMMAND(SREM, "SREM", NULL, -3, INDEX, 1)
COMMAND(SSCAN, "SSCAN", NULL, -3, INDEX, 1)
COMMAND(SSUBSCRIBE, "SSUBSCRIBE", NULL, -2, INDEX, 1)
COMMAND(STRLEN, "STRLEN", NULL, 2, INDEX, 1)
COMMAND(SUBSCRIBE, "SUBSCRIBE", NULL, -2, NONE, 0)
COMMAND(SUBSTR, "SUBSTR", NULL, 4, INDEX, 1)
COMMAND(SUNION, "SUNION", NULL, -2, INDEX, 1)
COMMAND(SUNIONSTORE, "SUNIONSTORE", NULL, -3, INDEX, 1)
COMMAND(SUNSUBSCRIBE, "SUNSUBSCRIBE", NULL, -1, INDEX, 1)
COMMAND(SWAPDB, "SWAPDB", NULL, 3, NONE, 0)
COMMAND(SYNC, "SYNC", NULL, 1, NONE, 0)
COMMAND(TIME, "TIME", NULL, 1, NONE, 0)
COMMAND(TOUCH, "TOUCH", NULL, -2, INDEX, 1)
COMMAND(TTL, "TTL", NULL, 2, INDEX, 1)
COMMAND(TYPE, "TYPE", NULL, 2, INDEX, 1)
COMMAND(UNLINK, "UNLINK", NULL, -2, INDEX, 1)
COMMAND(UNSUBSCRIBE, "UNSUBSCRIBE", NULL, -1, NONE, 0)
COMMAND(UNWATCH, "UNWATCH", NULL, 1, NONE, 0)
COMMAND(WAIT, "WAIT", NULL, 3, NONE, 0)
COMMAND(WATCH, "WATCH", NULL, -2, INDEX, 1)
COMMAND(XACK, "XACK", NULL, -4, INDEX, 1)
COMMAND(XADD, "XADD", NULL, -5, INDEX, 1)
COMMAND(XAUTOCLAIM, "XAUTOCLAIM", NULL, -6, INDEX, 1)
COMMAND(XCLAIM, "XCLAIM", NULL, -6, INDEX, 1)
COMMAND(XDEL, "XDEL", NULL, -3, INDEX, 1)
COMMAND(XGROUP_CREATE, "XGROUP", "CREATE", -5, INDEX, 2)
COMMAND(XGROUP_CREATECONSUMER, "XGROUP", "CREATECONSUMER", 5, INDEX, 2)
COMMAND(XGROUP_DELCONSUMER, "XGROUP", "DELCONSUMER", 5, INDEX, 2)
COMMAND(XGROUP_DESTROY, "XGROUP", "DESTROY", 4, INDEX, 2)
COMMAND(XGROUP_HELP, "XGROUP", "HELP", 2, NONE, 0)
COMMAND(XGROUP_SETID, "XGROUP", "SETID", -5, INDEX, 2)
COMMAND(XINFO_CONSUMERS, "XINFO", "CONSUMERS", 4, INDEX, 2)
COMMAND(XINFO_GROUPS, "XINFO", "GROUPS", 3, INDEX, 2)
COMMAND(XINFO_HELP, "XINFO", "HELP", 2, NONE, 0)
COMMAND(XINFO_STREAM, "XINFO", "STREAM", -3, INDEX, 2)
COMMAND(XLEN, "XLEN", NULL, 2, INDEX, 1)
COMMAND(XPENDING, "XPENDING", NULL, -3, INDEX, 1)
COMMAND(XRANGE, "XRANGE", NULL, -4, INDEX, 1)
COMMAND(XREAD, "XREAD", NULL, -4, UNKNOWN, 0)
COMMAND(XREADGROUP, "XREADGROUP", NULL, -7, UNKNOWN, 0)
COMMAND(XREVRANGE, "XREVRANGE", NULL, -4, INDEX, 1)
COMMAND(XSETID, "XSETID", NULL, -3, INDEX, 1)
COMMAND(XTRIM, "XTRIM", NULL, -4, INDEX, 1)
COMMAND(ZADD, "ZADD", NULL, -4, INDEX, 1)
COMMAND(ZCARD, "ZCARD", NULL, 2, INDEX, 1)
COMMAND(ZCOUNT, "ZCOUNT", NULL, 4, INDEX, 1)
COMMAND(ZDIFF, "ZDIFF", NULL, -3, KEYNUM, 1)
COMMAND(ZDIFFSTORE, "ZDIFFSTORE", NULL, -4, INDEX, 1)
COMMAND(ZINCRBY, "ZINCRBY", NULL, 4, INDEX, 1)
COMMAND(ZINTER, "ZINTER", NULL, -3, KEYNUM, 1)
COMMAND(ZINTERCARD, "ZINTERCARD", NULL, -3, KEYNUM, 1)
COMMAND(ZINTERSTORE, "ZINTERSTORE", NULL, -4, INDEX, 1)
COMMAND(ZLEXCOUNT, "ZLEXCOUNT", NULL, 4, INDEX, 1)
COMMAND(ZMPOP, "ZMPOP", NULL, -4, KEYNUM, 1)
COMMAND(ZMSCORE, "ZMSCORE", NULL, -3, INDEX, 1)
COMMAND(ZPOPMAX, "ZPOPMAX", NULL, -2, INDEX, 1)
COMMAND(ZPOPMIN, "ZPOPMIN", NULL, -2, INDEX, 1)
COMMAND(ZRANDMEMBER, "ZRANDMEMBER", NULL, -2, INDEX, 1)
COMMAND(ZRANGE, "ZRANGE", NULL, -4, INDEX, 1)
COMMAND(ZRANGEBYLEX, "ZRANGEBYLEX", NULL, -4, INDEX, 1)
COMMAND(ZRANGEBYSCORE, "ZRANGEBYSCORE", NULL, -4, INDEX, 1)
COMMAND(ZRANGESTORE, "ZRANGESTORE", NULL, -5, INDEX, 1)
COMMAND(ZRANK, "ZRANK", NULL, -3, INDEX, 1)
COMMAND(ZREM, "ZREM", NULL, -3, INDEX, 1)
COMMAND(ZREMRANGEBYLEX, "ZREMRANGEBYLEX", NULL, 4, INDEX, 1)
COMMAND(ZREMRANGEBYRANK, "ZREMRANGEBYRANK", NULL, 4, INDEX, 1)
COMMAND(ZREMRANGEBYSCORE, "ZREMRANGEBYSCORE", NULL, 4, INDEX, 1)
COMMAND(ZREVRANGE, "ZREVRANGE", NULL, -4, INDEX, 1)
COMMAND(ZREVRANGEBYLEX, "ZREVRANGEBYLEX", NULL, -4, INDEX, 1)
COMMAND(ZREVRANGEBYSCORE, "ZREVRANGEBYSCORE", NULL, -4, INDEX, 1)
COMMAND(ZREVRANK, "ZREVRANK", NULL, -3, INDEX, 1)
COMMAND(ZSCAN, "ZSCAN", NULL, -3, INDEX, 1)
COMMAND(ZSCORE, "ZSCORE", NULL, 3, INDEX, 1)
COMMAND(ZUNION, "ZUNION", NULL, -3, KEYNUM, 1)
COMMAND(ZUNIONSTORE, "ZUNIONSTORE", NULL, -4, INDEX, 1)

#!/bin/bash
#
# Simulate a 2 node cluster.
# - Node 1 will handle slotmap updates and redirects.
# - Node 2 will simulate a short and temporary network issue
#   that trigger the client to reconnect.
#
# Usage: $0 /path/to/clusterclient-binary

clientprog=${1:-./clusterclient}
testname=reconnect-failure-test

# Sync process just waiting for server to be ready to accept connection.
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid1=$!
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid2=$!

# Start simulated redis node #1
timeout 5s ./simulated-redis.pl -p 7401 -d --sigcont $syncpid1 <<'EOF' &
# Inital slotmap
EXPECT CONNECT
EXPECT ["CLUSTER", "SLOTS"]
SEND [[0, 6000, ["127.0.0.1", 7401, "nodeid1"]],[6001, 16383, ["127.0.0.1", 7402, "nodeid2"]]]
EXPECT CLOSE

EXPECT CONNECT
EXPECT ["SET", "bar", "initial"]
SEND +OK

# A reconnect failure triggers a search for an available node
EXPECT ["PING"]
SEND +PONG
EXPECT ["SET", "foo", "second"]
SEND -MOVED 12182 127.0.0.1:7402

# Since maxretry=2 a MOVED triggers a slotmap update (no slotmap change in this test)
EXPECT CONNECT
EXPECT ["CLUSTER", "SLOTS"]
SEND [[0, 6000, ["127.0.0.1", 7401, "nodeid1"]],[6001, 16383, ["127.0.0.1", 7402, "nodeid2"]]]
EXPECT CLOSE

# A reconnect failure triggers a new search for an available node
EXPECT ["PING"]
SEND +PONG

# Max retry exhausted

EXPECT CLOSE
EOF
server1=$!

# Start simulated redis node #2
timeout 5s ./simulated-redis.pl -p 7402 -d --sigcont $syncpid2 <<'EOF' &
EXPECT CONNECT
EXPECT ["SET", "foo", "initial"]
SEND +OK
CLOSE
EOF
server2=$!

# Wait until both nodes are ready to accept client connections
wait $syncpid1 $syncpid2;

# Run client
timeout 4s "$clientprog" 127.0.0.1:7401 > "$testname.out" <<'EOF'
SET foo initial
SET bar initial

SET foo first
SET foo second

EOF
clientexit=$?

# Wait for servers to exit
wait $server1; server1exit=$?
wait $server2; server2exit=$?

# Check exit statuses
if [ $server1exit -ne 0 ]; then
    echo "Simulated server #1 exited with status $server1exit"
    exit $server1exit
fi
if [ $server2exit -ne 0 ]; then
    echo "Simulated server #2 exited with status $server2exit"
    exit $server2exit
fi
if [ $clientexit -ne 0 ]; then
    echo "$clientprog exited with status $clientexit"
    exit $clientexit
fi

# Check the output from clusterclient
expected="OK
OK
error: Server closed the connection
error: too many cluster retries"

cmp "$testname.out" <(echo "$expected") || exit 99

# Clean up
rm "$testname.out"

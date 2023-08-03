#!/bin/bash
#
# Verify that commands can be sent using the low-level API to specific
# nodes. The testcase will send each command to all known nodes and
# verify the behaviour when a node is removed from the cluster.
#
# First the command DBSIZE is sent to all (two) nodes successfully,
# then the second node is shutdown. Following DBSIZE commands are
# also sent to all known nodes.
#
# Currently this testcase indicates issues:
# - The cluster slotmap is not updated as the retry-callback does.
# - Connection errors are propagated to the following callback, even
#   for command callbacks from other nodes.
#
# Usage: $0 /path/to/clusterclient-binary

clientprog=${1:-./clusterclient}
testname=dbsize-to-all-nodes-during-scaledown-test

# Sync processes waiting for CONT signals.
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid1=$!;
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid2=$!;

# Start simulated redis node #1
timeout 5s ./simulated-redis.pl -p 7401 -d --sigcont $syncpid1 <<'EOF' &
EXPECT CONNECT
EXPECT ["CLUSTER", "SLOTS"]
SEND [[0, 8383, ["127.0.0.1", 7401, "nodeid7401"]], [8384, 16383, ["127.0.0.1", 7402, "nodeid7402"]]]
EXPECT CLOSE
EXPECT CONNECT
EXPECT ["DBSIZE"]
SEND 10
EXPECT ["DBSIZE"]
SEND 11
EXPECT ["DBSIZE"]
SEND 12
EXPECT CLOSE
EOF
server1=$!

# Start simulated redis node #2
timeout 5s ./simulated-redis.pl -p 7402 -d --sigcont $syncpid2 <<'EOF' &
EXPECT CONNECT
EXPECT ["DBSIZE"]
SEND 20
CLOSE
EOF
server2=$!

# Wait until both nodes are ready to accept client connections
wait $syncpid1 $syncpid2;

# Run client
timeout 5s "$clientprog" 127.0.0.1:7401 > "$testname.out" <<'EOF'
!all
DBSIZE
DBSIZE
DBSIZE
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
expected="10
20
error: Server closed the connection
error: Server closed the connection
error: Connection refused
error: Connection refused"

echo "$expected" | diff -u - "$testname.out" || exit 99

# Clean up
rm "$testname.out"

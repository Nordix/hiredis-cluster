#!/bin/sh
#
# Simulate a 3 node cluster where `nodeid3` is removed after the initial cluster
# topology has been sent to the client. The client will attempt to connect to
# the missing node, but due to failure the request is sent to any active node.
# In the testcase this results in a topology refresh due to MOVED.
#
# Usage: $0 /path/to/clusterclient-binary

clientprog=${1:-./clusterclient}
testname=cluster-scale-down-test

# Sync processes waiting for CONT signals.
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid1=$!;
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid2=$!;

# Start simulated redis node #1
timeout 5s ./simulated-redis.pl -p 7401 -d --sigcont $syncpid1 <<'EOF' &
EXPECT CONNECT
EXPECT ["CLUSTER", "SLOTS"]
SEND [[0, 5000, ["127.0.0.1", 7401, "nodeid1"]],[5001, 10000, ["127.0.0.1", 7402, "nodeid2"]],[10001, 16383, ["127.0.0.1", 7403, "nodeid3"]]]
EXPECT CLOSE
EXPECT CONNECT
EXPECT ["PING"]
SEND +PONG
EXPECT ["GET", "foo"]
SEND -MOVED 12182 127.0.0.1:7402
EXPECT CONNECT
EXPECT ["CLUSTER", "SLOTS"]
SEND [[0, 8000, ["127.0.0.1", 7401, "nodeid1"]],[8001, 16383, ["127.0.0.1", 7402, "nodeid2"]]]
EXPECT CLOSE
EXPECT CLOSE
EOF
server1=$!

# Start simulated redis node #2
timeout 5s ./simulated-redis.pl -p 7402 -d --sigcont $syncpid2 <<'EOF' &
EXPECT CONNECT
EXPECT ["GET", "foo"]
SEND "bar"
EXPECT CLOSE
EOF
server2=$!

# Wait until both nodes are ready to accept client connections
wait $syncpid1 $syncpid2;

# Run client
echo 'GET foo' | timeout 3s "$clientprog" 127.0.0.1:7401 > "$testname.out"
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
printf 'bar\n' | cmp "$testname.out" - || exit 99

# Clean up
rm "$testname.out"

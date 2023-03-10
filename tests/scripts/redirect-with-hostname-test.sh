#!/bin/sh
#
# Verify that redirects with hostname are handled.
#
# Redis 7.0 introduced the config `cluster-preferred-endpoint-type` which
# controls how the endpoint is returned in ASK/MOVED redirects, and in
# CLUSTER SLOTS as well. This testcase verifies correct handling when
# Redis returns hostnames instead of IP's.
#
# Redis 7.0 also adds additional metadata in CLUSTER SLOTS and this test
# uses a black hole address to make sure this is not used, but accepted.
#
# Usage: $0 /path/to/clusterclient-binary

clientprog=${1:-./clusterclient}
testname=redirect-with-hostname-test

# Sync processes waiting for CONT signals.
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid1=$!;
perl -we 'use sigtrap "handler", sub{exit}, "CONT"; sleep 1; die "timeout"' &
syncpid2=$!;

# Start simulated redis node #1
timeout 5s ./simulated-redis.pl -p 7403 -d --sigcont $syncpid1 <<'EOF' &
# Inital slotmap update
EXPECT CONNECT
EXPECT ["CLUSTER", "SLOTS"]
SEND [[0, 16383, ["localhost", 7403, "nodeid7403", ["ip", "192.168.254.254"]]]]
EXPECT CLOSE

EXPECT CONNECT
EXPECT ["GET", "foo"]
SEND -ASK 12182 localhost:7404

EXPECT ["GET", "foo"]
SEND -MOVED 12182 localhost:7404

# Slotmap updated due to MOVED
EXPECT CONNECT
EXPECT ["CLUSTER", "SLOTS"]
SEND [[0, 16383, ["localhost", 7404, "nodeid7404", ["ip", "192.168.254.254"]]]]
EXPECT CLOSE
EXPECT CLOSE
EOF
server1=$!

# Start simulated redis node #2
timeout 5s ./simulated-redis.pl -p 7404 -d --sigcont $syncpid2 <<'EOF' &
EXPECT CONNECT
EXPECT ["ASKING"]
SEND +OK
EXPECT ["GET", "foo"]
SEND "bar"
EXPECT ["GET", "foo"]
SEND "bar"
EXPECT CLOSE
EOF
server2=$!

# Wait until both nodes are ready to accept client connections
wait $syncpid1 $syncpid2;

# Run client
timeout 3s "$clientprog" localhost:7403 > "$testname.out" <<'EOF'
GET foo
GET foo
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
printf 'bar\nbar\n' | cmp "$testname.out" - || exit 99

# Clean up
rm "$testname.out"

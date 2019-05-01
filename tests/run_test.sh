#! /bin/sh

# Usage:
#   run_test.sh <TOP_BUILD_DIR> <TEST_PROGRAM>
#
# Run TEST_PROGRAM within a proper environment; this includes a D-Bus session
# and any environment variable needed for the succesful completion of the test.
#
# If the TEST_WRAPPER environment variable is set, then it will be executed and
# the test program will be passed as an argument to it; this can be useful, for
# example, to run the tests in valgrind or strace.

set -e

TOP_BUILD_DIR="$1"
TEST_PROGRAM="$2"

export LC_ALL=C
export QT_PLUGIN_PATH="${TOP_BUILD_DIR}/src/engine/"

OUTPUT=$(dbus-daemon --session --print-address '' --print-pid '' --fork)
export DBUS_SESSION_BUS_ADDRESS=$(echo "$OUTPUT" | head -1)
DBUS_DAEMON_PID=$(echo "$OUTPUT" | tail -1)

cleanUp() {
    echo "Killing the temporary D-Bus daemon"
    kill "$DBUS_DAEMON_PID"
}

trap cleanUp EXIT INT TERM

$TEST_WRAPPER $TEST_PROGRAM

trap - EXIT
cleanUp

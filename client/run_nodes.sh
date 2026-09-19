#!/usr/bin/env bash
# Starts N simulated nodes in the background (default 5) against the given host.
#   ./run_nodes.sh [count] [host]
# Stop them with ./stop_nodes.sh
set -euo pipefail
NODE_COUNT="${1:-5}"
SERVER_HOST="${2:-${TELEP_SERVER_HOST:-localhost}}"
cd "$(dirname "$0")"
mkdir -p logs
for index in $(seq 1 "$NODE_COUNT"); do
    node_id=$(printf 'NODE%02d' "$index")
    python3 node.py --id "$node_id" --host "$SERVER_HOST" --interval 2 > "logs/$node_id.log" 2>&1 &
    echo "$!" >> logs/pids
    echo "started $node_id (pid $!) -> logs/$node_id.log"
done

#!/usr/bin/env bash
# Sends SIGTERM to the nodes started by run_nodes.sh; each one prints its total and exits.
cd "$(dirname "$0")"
[ -f logs/pids ] || { echo "no nodes running"; exit 0; }
while read -r pid; do kill "$pid" 2>/dev/null && echo "stopped pid $pid"; done < logs/pids
rm -f logs/pids

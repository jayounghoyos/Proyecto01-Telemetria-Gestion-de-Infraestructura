#!/usr/bin/env bash
# Run manually on the chosen test machine. This creates a real capture only when invoked.
set -euo pipefail
interface=${1:?Usage: bash scripts/capture_traffic.sh INTERFACE OUTPUT.pcap}
output=${2:?Supply a new output .pcap path}
if [[ -e "$output" ]]; then echo "Refusing to overwrite existing capture" >&2; exit 1; fi
command -v tcpdump >/dev/null || { echo "Install tcpdump first" >&2; exit 1; }
printf 'Capturing on %s to %s; stop with Ctrl-C.
' "$interface" "$output"
exec tcpdump -i "$interface" -nn -s 0 -w "$output" 'tcp port 5000 or udp port 5001'

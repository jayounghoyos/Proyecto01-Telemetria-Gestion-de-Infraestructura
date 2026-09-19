#!/usr/bin/env bash
# Points the DuckDNS name to the current public IP of this instance.
# In AWS Academy the instance is stopped when the session ends and comes back
# with a different IP, so this runs as a systemd service on every boot.
# Reads /etc/duckdns.conf with DUCKDNS_SUBDOMAIN=... and DUCKDNS_TOKEN=...
set -euo pipefail
source /etc/duckdns.conf

IMDS_TOKEN=$(curl -s -X PUT "http://169.254.169.254/latest/api/token" -H "X-aws-ec2-metadata-token-ttl-seconds: 60")
PUBLIC_IP=$(curl -s -H "X-aws-ec2-metadata-token: $IMDS_TOKEN" "http://169.254.169.254/latest/meta-data/public-ipv4")
RESULT=$(curl -s "https://www.duckdns.org/update?domains=${DUCKDNS_SUBDOMAIN}&token=${DUCKDNS_TOKEN}&ip=${PUBLIC_IP}")
logger -t duckdns "${DUCKDNS_SUBDOMAIN}.duckdns.org -> ${PUBLIC_IP}: ${RESULT}"
echo "${DUCKDNS_SUBDOMAIN}.duckdns.org -> ${PUBLIC_IP}: ${RESULT}"
[ "$RESULT" = "OK" ]

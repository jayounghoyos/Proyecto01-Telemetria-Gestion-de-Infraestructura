#!/usr/bin/env bash
# Deploys the server to the EC2 instance: uploads the code, builds the image there
# and starts the container. Safe to run again after every change.
#   export TELEP_SSH_TARGET=ubuntu@my-name.duckdns.org   # never an IP in the repo
#   export TELEP_SSH_KEY=~/.ssh/labsuser.pem
#   ./deploy/deploy.sh
set -euo pipefail
cd "$(dirname "$0")/.."
: "${TELEP_SSH_TARGET:?set TELEP_SSH_TARGET, e.g. ubuntu@my-name.duckdns.org}"
SSH_KEY="${TELEP_SSH_KEY:-$HOME/.ssh/labsuser.pem}"
SSH_OPTIONS=(-i "$SSH_KEY" -o StrictHostKeyChecking=accept-new)
REMOTE_DIR=/opt/telemetria

remote() { ssh "${SSH_OPTIONS[@]}" "$TELEP_SSH_TARGET" "$@"; }
step()   { printf '\n\033[38;5;75m%s\033[0m\n' "$1"; }

step "1/4 waiting for the instance to finish its first boot"
for attempt in $(seq 1 30); do
    remote 'test -f /opt/telemetria/READY' 2>/dev/null && break
    [ "$attempt" = 30 ] && { echo "the instance did not finish booting; check /var/log/cloud-init-output.log"; exit 1; }
    sleep 5
done

step "2/4 uploading server, web page, compose files and DuckDNS kit"
rsync -az --delete -e "ssh ${SSH_OPTIONS[*]}" server/ "$TELEP_SSH_TARGET:$REMOTE_DIR/server/"
[ -d web ] && rsync -az --delete -e "ssh ${SSH_OPTIONS[*]}" web/ "$TELEP_SSH_TARGET:$REMOTE_DIR/web/"
rsync -az -e "ssh ${SSH_OPTIONS[*]}" Dockerfile docker-compose.yml .dockerignore deploy/duckdns-update.sh deploy/duckdns.service "$TELEP_SSH_TARGET:$REMOTE_DIR/"

step "3/4 installing the DuckDNS updater (needs /etc/duckdns.conf on the instance)"
remote "cd $REMOTE_DIR && sudo install -m 755 duckdns-update.sh /usr/local/bin/ && sudo install -m 644 duckdns.service /etc/systemd/system/ \
        && if [ -f /etc/duckdns.conf ]; then sudo systemctl enable --now duckdns.service && sudo /usr/local/bin/duckdns-update.sh; \
           else echo '   (no /etc/duckdns.conf yet: create it with DUCKDNS_SUBDOMAIN and DUCKDNS_TOKEN)'; fi"

step "4/4 building the image and starting the container"
remote "cd $REMOTE_DIR && docker compose up -d --build --force-recreate && docker compose ps && docker port telemetry-server"

echo
echo "done. Check it from this machine using the name, never the IP:"
echo "   curl http://${TELEP_SSH_TARGET#*@}:8080/status"

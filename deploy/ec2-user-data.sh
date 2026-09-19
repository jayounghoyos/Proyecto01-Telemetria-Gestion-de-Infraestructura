#!/usr/bin/env bash
# User data for the EC2 instance (Ubuntu). Runs once as root on first boot.
# Paste it in "Advanced details -> User data" when launching the instance.
# It installs Docker; the code is uploaded later with deploy/deploy.sh.
set -eux
apt-get update -y
apt-get install -y docker.io docker-compose-v2 tcpdump
systemctl enable --now docker
usermod -aG docker ubuntu
mkdir -p /opt/telemetria && chown ubuntu:ubuntu /opt/telemetria
touch /opt/telemetria/READY

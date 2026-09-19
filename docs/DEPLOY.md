# Deployment notes

We run the server on an EC2 instance from the AWS Academy Learner Lab, inside Docker, and we
reach it through the name telemetria-eafit.duckdns.org. No IP address is written anywhere in
the code; the clients resolve the name with DNS every time they start.

## The instance

Ubuntu Server on a t2.micro in us-east-1, launched from the console with the lab's key pair
(vockey, downloaded as labsuser.pem). The security group has four inbound rules: 22/tcp for
SSH, 5000/tcp and 5001/udp for TELEP, and 8080/tcp for the web page. The UDP rule was the one
we almost missed: with it as TCP the web page still answers and the nodes still send, but the
server never gets a datagram.

Docker was installed by hand the first time (apt-get install docker.io docker-compose-v2). The
same commands are in deploy/ec2-user-data.sh in case the instance has to be created again;
pasted as user data they run on the first boot.

## The name

The Learner Lab stops the instance when the four-hour session ends, and on the next start it
gets a different public IP. We could not use Route 53 in the lab, so we registered a subdomain
at DuckDNS and wrote deploy/duckdns-update.sh: it asks the instance metadata service for the
current public IP and sends it to DuckDNS. It runs as a systemd unit on every boot
(deploy/duckdns.service), reading the token from /etc/duckdns.conf, which stays on the
instance and never in the repo. Docker brings the container back on its own, so after Start
Lab and Start instance the whole thing is up again in about a minute with nothing to change
on the clients. We checked this: the IP went from 34.226.153.93 to 13.218.65.204 and then to
3.85.238.127 across sessions and the nodes kept working with the same name.

## Updating the code

deploy/deploy.sh copies server/, web/ and the compose files to /opt/telemetria on the
instance with rsync, installs the DuckDNS unit, builds the image there and recreates the
container. We run it from our machines after every change:

    export TELEP_SSH_TARGET=ubuntu@telemetria-eafit.duckdns.org
    export TELEP_SSH_KEY=~/.ssh/labsuser.pem
    ./deploy/deploy.sh

Then we test from outside the instance with the name only, for example
curl http://telemetria-eafit.duckdns.org:8080/status or the operator client with
--host telemetria-eafit.duckdns.org. On the instance, docker compose logs -f in
/opt/telemetria shows registrations, alerts and dropped datagrams as they happen.

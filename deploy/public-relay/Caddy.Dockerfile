FROM caddy:2.11.4-alpine

# The official binary carries cap_net_bind_service for direct :80/:443 use.
# This deployment listens on unprivileged container ports 8080/8443, so remove
# the unused file capability. Otherwise a cap-drop=ALL runtime cannot exec it.
USER root
RUN setcap -r /usr/bin/caddy

USER 65532:65532

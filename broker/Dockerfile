# wine-webapp-broker: one app container per signed-in user (docs/broker.md).
#   docker build -t wine-webapp-broker broker/
ARG DEBIAN=debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
FROM ${DEBIAN}
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3-aiohttp tini \
 && rm -rf /var/lib/apt/lists/*
COPY wine-webapp-broker /usr/local/bin/
EXPOSE 8000
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/wine-webapp-broker"]
CMD ["/etc/wine-webapp-broker/broker.toml"]

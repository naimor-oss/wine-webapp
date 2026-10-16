# wine-webapp-gate: the broker's sign-in gate, Kerberos with a password
# fallback (docs/broker.md, "The sign-in gate").
#   docker build -t wine-webapp-gate gate/
ARG DEBIAN=debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
FROM ${DEBIAN}
RUN apt-get update \
 && apt-get install -y --no-install-recommends apache2 libapache2-mod-auth-gssapi tini \
 && rm -rf /var/lib/apt/lists/* \
 && a2enmod ssl proxy proxy_http headers session session_cookie auth_gssapi \
 && a2dissite 000-default \
 && echo 'Listen 443' > /etc/apache2/ports.conf
COPY gate.conf /etc/apache2/sites-enabled/wine-webapp-gate.conf
COPY wine-webapp-gate /usr/local/bin/
EXPOSE 443
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/wine-webapp-gate"]

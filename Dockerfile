# syntax=docker/dockerfile:1
#
# wine-webapp: run one Windows desktop application under Wine and serve it to
# a web browser. Targets:
#   winedlls  patched Wine DLLs built from our Wine fork (see docs/wine-patches.md)
#   tools     helper programs run under Wine (tools/), e.g. the window list
#   base      Wine + app contract (docs/app-contract.md), no display
#   vnc       base + TigerVNC/noVNC front end (docs/frontends.md)
#
#   docker build --target vnc -t wine-webapp:vnc .
#   # local Wine fork checkout instead of the published branch:
#   docker build --target vnc --build-context winesrc=../wine -t wine-webapp:vnc .

ARG DEBIAN=debian:trixie-slim

# Wine release, and the WineHQ Debian package version built from it. The
# patched DLLs are built from WINE_FORK_REF, which must be based on the same
# release. Change all three together.
ARG WINE_PKG=11.0.0.0~trixie-1
ARG WINE_FORK=https://github.com/Naimor-OSS/wine.git
ARG WINE_FORK_REF=naimor/wine-11.0
# 32-bit PE modules rebuilt from the fork and dropped over WineHQ's copies.
ARG WINE_PATCHED_DLLS="comctl32_v6.dll user32.dll"

# --- Wine source from our fork (overridable with --build-context winesrc=...)
FROM scratch AS winesrc
ARG WINE_FORK WINE_FORK_REF
ADD ${WINE_FORK}#${WINE_FORK_REF} /

# --- patched Wine DLLs
FROM ${DEBIAN} AS winedlls
ARG WINE_PATCHED_DLLS
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      make gcc libc6-dev flex bison gcc-mingw-w64-i686 \
 && rm -rf /var/lib/apt/lists/*
COPY build/build-wine-dlls.sh /usr/local/bin/build-wine-dlls
RUN --mount=from=winesrc,target=/src/wine \
    sh /usr/local/bin/build-wine-dlls /src/wine /out $WINE_PATCHED_DLLS

# --- helper programs run under Wine (same mingw toolchain)
FROM winedlls AS tools
COPY tools/winlist/winlist.c /src/
RUN mkdir -p /out && i686-w64-mingw32-gcc -O2 -Wall -o /out/winlist.exe /src/winlist.c

# --- base: Wine + app contract
FROM ${DEBIAN} AS base
ARG WINE_PKG
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# WineHQ packages: Debian trixie's own wine64 lacks the 32-bit PE DLLs, and
# most line-of-business Windows apps are 32-bit, so the i386 side is needed.
RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates wget gnupg \
 && mkdir -pm755 /etc/apt/keyrings \
 && wget -qO /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key \
 && wget -qNP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/debian/dists/trixie/winehq-trixie.sources \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      winehq-stable=$WINE_PKG wine-stable=$WINE_PKG \
      wine-stable-amd64=$WINE_PKG wine-stable-i386:i386=$WINE_PKG \
      tini fontconfig fonts-liberation \
 && apt-get purge -y gnupg wget && apt-get autoremove -y \
 && rm -rf /var/lib/apt/lists/* /usr/share/doc/* /usr/share/man/*
COPY --from=winedlls /out/ /opt/wine-stable/lib/wine/i386-windows/
COPY --from=tools /out/winlist.exe /usr/local/lib/wine-webapp/

RUN useradd -m -u 1000 -s /bin/bash webapp
ENV PATH=/opt/wine-stable/bin:$PATH \
    WINEPREFIX=/home/webapp/.wine WINEARCH=win64 WINEDEBUG=-all \
    WINEDLLOVERRIDES="mscoree,mshtml=" \
    WEBAPP_SCREEN=1600x900 WEBAPP_BACKGROUND="#333333" WEBAPP_NAME=""

COPY rootfs-base/ /
# strip CRs in case the scripts were checked out with Windows line endings
RUN sed -i 's/\r$//' /usr/local/bin/wine-webapp-* && chmod +x /usr/local/bin/wine-webapp-*

USER webapp
RUN wine-webapp-init-prefix
USER root
ENTRYPOINT ["/usr/bin/tini", "--"]

# --- front end: TigerVNC + noVNC
FROM base AS vnc
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      tigervnc-standalone-server novnc python3-websockify hsetroot \
 && rm -rf /var/lib/apt/lists/* /usr/share/doc/* /usr/share/man/*
COPY rootfs-vnc/ /
RUN sed -i 's/\r$//' /usr/local/bin/wine-webapp-* && chmod +x /usr/local/bin/wine-webapp-*
EXPOSE 8080
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/wine-webapp-frontend"]

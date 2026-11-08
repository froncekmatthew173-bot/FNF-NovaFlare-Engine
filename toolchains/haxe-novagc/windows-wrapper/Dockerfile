FROM debian:12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates haxe neko libmbedtls14 libpcre2-8-0 zlib1g \
    && rm -rf /var/lib/apt/lists/*

# The local hxcpp is a Windows-side development mapping. Expose its mounted
# Linux path while forwarding every other query to Debian haxelib.
RUN printf '#!/bin/sh\nif [ "$1" = "path" ] && [ "$2" = "hxcpp" ]; then\n  echo /workspace/hxcpp/src/\n  echo "-D hxcpp=0.1.0"\n  exit 0\nfi\nexec /usr/bin/haxelib "$@"\n' > /usr/local/bin/haxelib \
    && chmod +x /usr/local/bin/haxelib

FROM debian:stable-slim AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    pkg-config \
    libmicrohttpd-dev \
    libcjson-dev \
    python3 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /deps

RUN git clone --depth 1 --branch v1.4.0 https://github.com/open62541/open62541.git && \
    cmake -S open62541 -B open62541-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DUA_ENABLE_ENCRYPTION=OFF \
        -DUA_ENABLE_ENCRYPTION_OPENSSL=OFF && \
    cmake --build open62541-build && \
    cmake --install open62541-build --prefix /usr/local

WORKDIR /app

COPY CMakeLists.txt .
COPY src ./src

RUN cmake -S . -B build && \
    cmake --build build --config Release

FROM debian:stable-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    libmicrohttpd12 \
    libcjson1 \
    gosu && \
    rm -rf /var/lib/apt/lists/* && \
    useradd -m opcua

WORKDIR /app

COPY --from=build /app/build/opcua_server /usr/local/bin/opcua_server
COPY --from=build /usr/local/lib/libopen62541.* /usr/local/lib/
COPY web/opcua-server /app/web/

COPY docker/opcua-entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh && ldconfig

ENV OPCUA_HTTP_WEB_ROOT=/app/web
ENV OPCUA_SERVER_CONFIG_PATH=/data/server-config.json
ENV OPCUA_HTTP_ADDR=:8081

EXPOSE 4840 8081

ENTRYPOINT ["/entrypoint.sh"]

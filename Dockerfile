FROM debian:stable-slim AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
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

RUN useradd -m opcua

WORKDIR /app

COPY --from=build /app/build/opcua_server /usr/local/bin/opcua_server
COPY --from=build /usr/local/lib/libopen62541.* /usr/local/lib/

RUN ldconfig

EXPOSE 4840

USER opcua

ENTRYPOINT ["opcua_server"]


#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/dist-linux64"
IMAGE_NAME="opcua-bridge-linux64-builder"

docker build --platform linux/amd64 -f - -t "${IMAGE_NAME}" "${SCRIPT_DIR}" <<'DOCKERFILE'
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    build-essential \
    cmake \
    git \
    python3 \
    qt6-base-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /deps
RUN git clone --depth 1 --branch v1.4.0 https://github.com/open62541/open62541.git && \
    cmake -S open62541 -B open62541-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DUA_ENABLE_ENCRYPTION=OFF \
        -DUA_ENABLE_ENCRYPTION_OPENSSL=OFF && \
    cmake --build open62541-build && \
    cmake --install open62541-build --prefix /usr/local

WORKDIR /src
COPY CMakeLists.txt .
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target opcua_tcp_bridge opcua_tcp_bridge_gui
DOCKERFILE

mkdir -p "${OUTPUT_DIR}"
docker create --name linux64-build "${IMAGE_NAME}"
docker cp linux64-build:/src/build/opcua_tcp_bridge "${OUTPUT_DIR}/"
docker cp linux64-build:/src/build/opcua_tcp_bridge_gui "${OUTPUT_DIR}/"
docker rm linux64-build

echo "Built for x86_64 Linux:"
echo "  ${OUTPUT_DIR}/opcua_tcp_bridge"
echo "  ${OUTPUT_DIR}/opcua_tcp_bridge_gui"
file "${OUTPUT_DIR}/opcua_tcp_bridge" "${OUTPUT_DIR}/opcua_tcp_bridge_gui"

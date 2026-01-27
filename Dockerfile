FROM debian:stable-slim AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libopen62541-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY CMakeLists.txt .
COPY src ./src

RUN cmake -S . -B build && \
    cmake --build build --config Release

FROM debian:stable-slim

RUN useradd -m opcua

WORKDIR /app

COPY --from=build /app/build/opcua_server /usr/local/bin/opcua_server

EXPOSE 4840

USER opcua

ENTRYPOINT ["opcua_server"]


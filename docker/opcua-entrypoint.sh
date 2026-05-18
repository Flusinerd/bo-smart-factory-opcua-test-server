#!/bin/sh
set -e

if [ -d /data ]; then
    chown -R opcua:opcua /data 2>/dev/null || true
fi

exec gosu opcua opcua_server

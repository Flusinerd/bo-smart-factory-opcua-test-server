## OPC UA Binary Sensors Demo Server

This is a small C application using the open62541 library that exposes an OPC UA
server with multiple binary sensors. The sensors toggle on and off on a timer by
default and can be overridden manually by an OPC UA client.

### Features

- **Multiple binary sensors**: `Sensor1` .. `Sensor4` under the `Sensors` folder
  in the OPC UA address space.
- **Simulation**: Sensors automatically toggle between `true` and `false` every
  second.
- **Manual override**: Writing a value to a sensor from a client overrides the
  simulation for that sensor.
- **Reset overrides**: Writing `true` to the `ResetOverrides` variable clears
  all overrides and resumes simulation for all sensors.

### Configuration web UI

The server exposes an HTTP configuration UI (default port **8081**):

- **Namespace URI** — persisted to config; restart the server to apply.
- **Address space** — add folders and variables (Boolean, Int32, Double, String) under any parent folder; changes apply immediately and persist across restarts.
- **Simulation** — enable/disable auto-toggling, reset manual overrides on simulated Boolean sensors.

Open [http://localhost:8081](http://localhost:8081) after starting the server.

| Variable | Meaning |
|----------|---------|
| `OPCUA_SERVER_CONFIG_PATH` | Path to server config JSON (default `./server-config.json`, Docker: `/data/server-config.json`) |
| `OPCUA_NAMESPACE_URI` | Override namespace URI from environment |
| `OPCUA_HTTP_ADDR` | HTTP listen address (default `:8081`) |
| `OPCUA_HTTP_WEB_ROOT` | Static web assets directory (Docker: `/app/web`) |

The TCP bridge is a **standalone** program with its own UI on port 8080. Point it at `opc.tcp://<host>:4840` and browse to map newly added server nodes.

### Build and run with Docker

From the project root:

```bash
docker build -t opcua-binary-sensors .
docker run --rm -p 4840:4840 -p 8081:8081 \
  -v opcua_data:/data \
  -e OPCUA_SERVER_CONFIG_PATH=/data/server-config.json \
  opcua-binary-sensors
```

Or with Compose (server only):

```bash
docker compose up opcua --build
```

The server listens on `opc.tcp://0.0.0.0:4840`. Configuration persists in the `opcua_config` volume.

### Connect with an OPC UA client

1. Start the container as shown above.
2. Use an OPC UA client such as UaExpert.
3. Connect to the endpoint `opc.tcp://localhost:4840`.
4. Browse to `Objects -> Sensors`.
5. Observe `Sensor1` .. `Sensor4` toggling between `true` and `false`.
6. Write `true` or `false` to a sensor to override its value and stop toggling
   for that sensor.
7. Write `true` to `ResetOverrides` to clear all overrides and resume simulation
   for every sensor.

### Local build (without Docker)

Install dependencies (macOS example):

```bash
brew install open62541 libmicrohttpd cjson pkg-config
mkdir -p build && cd build
cmake ..
cmake --build . --config Release
OPCUA_HTTP_WEB_ROOT=../web/opcua-server ./opcua_server
```

## OPC UA TCP Bridge

The bridge connects to the OPC UA server as a client and streams sensor values
over TCP as MessagePack frames (see [PROTOCOL.md](PROTOCOL.md)). Multiple TCP
clients can subscribe to the same stream.

### Go bridge with web UI (recommended)

The `opcua-bridge` binary (Go) implements the bridge and a built-in web UI for
browsing the address space, editing sensor mappings, and starting or stopping
the TCP stream. Configuration JSON is compatible with the former Qt GUI
(`endpointUrl`, `namespaceUri`, `tcpPort`, `mappings`).

**Run locally** (requires Go 1.22+):

```bash
go run ./cmd/opcua-bridge
```

Open the UI at [http://localhost:8080](http://localhost:8080). The MessagePack
TCP server listens on port **9000** by default (override with
`OPCUA_TCP_BRIDGE_PORT`).

**Environment variables**:

| Variable | Meaning |
|----------|---------|
| `HTTP_ADDR` | HTTP listen address (default `:8080`) |
| `OPCUA_TCP_BRIDGE_PORT` | TCP port for MessagePack stream (default `9000`) |
| `CONFIG_PATH` | Optional path to a JSON config file; loaded at startup and written on **Save** from the UI (`PUT /api/config`) |
| `OPCUA_ENDPOINT` | Default OPC UA endpoint URL (overrides config file) |
| `OPCUA_NAMESPACE_URI` | Default namespace URI (overrides config file) |

**Docker** (bridge only):

```bash
docker build -f Dockerfile.bridge -t opcua-tcp-bridge .
docker run --rm -p 8080:8080 -p 9000:9000 \
  -e OPCUA_ENDPOINT=opc.tcp://host.docker.internal:4840 \
  -e OPCUA_NAMESPACE_URI=urn:binary-sensors-demo \
  opcua-tcp-bridge
```

**Docker Compose** (demo OPC UA server + bridge on one network):

```bash
docker compose up --build
```

The UI is on port **8080** and the TCP stream on **9000**; the bridge points at
`opc.tcp://opcua:4840` by default.

### Legacy Qt GUI and C bridge (deprecated)

The `opcua_tcp_bridge_gui` (Qt 6) and `opcua_tcp_bridge` (C) targets remain in
CMake for reference only; new development should use the Go bridge above.

### Connect to the TCP stream

Connect using `netcat` or any TCP client:

```bash
nc localhost 9000
```

You'll receive a continuous stream of MessagePack-encoded frames. See
[PROTOCOL.md](PROTOCOL.md) for the full wire format.

### Check frames from the CLI

**Raw bytes (hex):**

```bash
nc localhost 9000 | hexdump -C
```

**Decode with msgpack-tools** (macOS/Linux via [Homebrew](https://formulae.brew.sh/formula/msgpack-tools)):

```bash
brew install msgpack-tools
nc localhost 9000 | msgpack2json -d
```

**Decode and pretty-print each frame** (requires Python 3 and `msgpack`):

```bash
pip install msgpack
nc localhost 9000 | python3 -c "
import sys, json, msgpack
unpacker = msgpack.Unpacker(sys.stdin.buffer, raw=False)
for obj in unpacker:
    print(json.dumps(obj, indent=2))
"
```

To print only the first few frames and exit (e.g. 5):

```bash
nc localhost 9000 | python3 -c "
import sys, json, msgpack
unpacker = msgpack.Unpacker(sys.stdin.buffer, raw=False)
for i, obj in enumerate(unpacker):
    print(json.dumps(obj, indent=2))
    if i >= 4:
        break
"
```

The bridge sends updates approximately every 300ms when sensor values change or
when new clients connect.

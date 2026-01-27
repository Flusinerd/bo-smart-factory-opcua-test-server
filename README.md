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

### Build and run with Docker

From the project root:

```bash
docker build -t opcua-binary-sensors .
docker run --rm -p 4840:4840 opcua-binary-sensors
```

The server will listen on `opc.tcp://0.0.0.0:4840`.

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

You can also build the server locally if you have `libopen62541-dev` and a basic
C toolchain installed:

```bash
brew install open62541
cd build
cmake ..
cmake --build . --config Release
./opcua_server
```

## OPC UA TCP Bridge

The project includes a second executable `opcua_tcp_bridge` that connects to the
OPC UA server as a client and exposes the sensor values as a binary TCP stream.
This allows multiple TCP clients to receive real-time sensor updates.

### Features

- **OPC UA client**: Connects to the running `opcua_server` and reads sensor
  values
- **TCP server**: Listens on port 9000 (configurable via `OPCUA_TCP_BRIDGE_PORT`
  env var)
- **Multi-client support**: Multiple TCP clients can connect simultaneously
- **Binary protocol**: Compact fixed-size frames with sensor state, sequence
  numbers, and timestamps
- **Automatic reconnection**: Handles OPC UA server disconnections with
  exponential backoff

### Build and run

Build both executables:

```bash
cd build
cmake ..
cmake --build . --config Release
```

Run the bridge (ensure `opcua_server` is running first):

```bash
./opcua_tcp_bridge
```

Or specify a custom TCP port:

```bash
OPCUA_TCP_BRIDGE_PORT=8080 ./opcua_tcp_bridge
```

### Connect to the TCP stream

Connect using `netcat` or any TCP client:

```bash
nc localhost 9000
```

You'll receive a continuous stream of binary frames. To inspect the raw bytes:

```bash
nc localhost 9000 | hexdump -C
```

### Binary Protocol Format

Each frame is exactly 18 bytes with the following structure (big-endian):

| Offset | Size    | Description                                                       |
| ------ | ------- | ----------------------------------------------------------------- |
| 0      | 1 byte  | Protocol version (currently `0x01`)                               |
| 1-8    | 8 bytes | Sequence number (`uint64`, big-endian)                            |
| 9-16   | 8 bytes | Timestamp in milliseconds since Unix epoch (`uint64`, big-endian) |
| 17     | 1 byte  | Sensor flags (bits 0-3: Sensor1-Sensor4 boolean values)           |

**Sensor flags byte:**

- Bit 0: Sensor1 value (1 = true, 0 = false)
- Bit 1: Sensor2 value (1 = true, 0 = false)
- Bit 2: Sensor3 value (1 = true, 0 = false)
- Bit 3: Sensor4 value (1 = true, 0 = false)
- Bits 4-7: Reserved (currently 0)

**Example frame (hex):**

```
01 00 00 00 00 00 00 00 01 00 00 00 00 17 8d 5f 4e 0f
│  │                    │                    │  │
│  │                    │                    │  └─ Sensor flags: 0x0F = all sensors true
│  │                    │                    └──── Timestamp (ms)
│  │                    └───────────────────────── Sequence: 1
│  └─────────────────────────────────────────────── Protocol version: 0x01
```

The bridge sends updates approximately every 300ms when sensor values change or
when new clients connect.

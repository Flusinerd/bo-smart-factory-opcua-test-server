# MessagePack Protocol Format for OPC UA TCP Bridge

## Frame Structure

- **Variable-size frames**: Each frame is a MessagePack-encoded map
- **Streaming**: Frames are sent continuously, one after another, with no delimiters
- **Parsing**: Clients must parse MessagePack objects sequentially from the TCP stream

## Message Format

Each frame is a MessagePack map containing the following top-level fields:

| Field       | Type           | Description                    |
|------------|----------------|--------------------------------|
| version    | byte (uint8)   | Protocol version (currently `0x01`) |
| sequence   | ulong (uint64) | Monotonic sequence number     |
| timestampMs| ulong (uint64) | Unix time milliseconds        |
| sensors    | array of maps  | One entry per sensor          |

## Sensor Entry Format

Each entry in the `sensors` array is a MessagePack map with the following fields:

| Field | Type   | Description                                                          |
|-------|--------|----------------------------------------------------------------------|
| id    | string | Sensor identifier (e.g. "Sensor1", "Sensor2", "Sensor3", "Sensor4") |
| type  | byte (uint8) | Type code: 0=bool, 1=int32, 2=int64, 3=float32, 4=float64, 5=string |
| value | (any)  | MessagePack value matching the type (bool, int, float, string)      |

### Type Codes

- `0`: Boolean (`true` or `false`)
- `1`: Signed 32-bit integer
- `2`: Signed 64-bit integer
- `3`: IEEE 754 single-precision float (32-bit)
- `4`: IEEE 754 double-precision float (64-bit)
- `5`: UTF-8 string

## Current Implementation

The reference bridge is the **Go** service (`opcua-bridge`): it maps configured
OPC UA variables to `sensors[]` entries (`id`, `type`, `value`) as described
above. The legacy C demo bridge sent fixed boolean sensors (`Sensor1`–`Sensor4`);
configurable mappings are the norm for the Go bridge.

## Example Frame Structure

```json
{
  "version": 1,
  "sequence": 72,
  "timestampMs": 1769516986221,
  "sensors": [
    {
      "id": "Sensor1",
      "type": 0,
      "value": true
    },
    {
      "id": "Sensor2",
      "type": 0,
      "value": true
    },
    {
      "id": "Sensor3",
      "type": 0,
      "value": true
    },
    {
      "id": "Sensor4",
      "type": 0,
      "value": true
    }
  ]
}
```

## Decoding Pseudocode

```python
import msgpack

def decode_frame(stream):
    unpacker = msgpack.Unpacker(stream)
    frame = unpacker.unpack()
    
    version = frame[b'version']
    sequence = frame[b'sequence']
    timestamp_ms = frame[b'timestampMs']
    sensors = frame[b'sensors']
    
    result = {
        'version': version,
        'sequence': sequence,
        'timestamp_ms': timestamp_ms,
        'sensors': []
    }
    
    for sensor in sensors:
        sensor_entry = {
            'id': sensor[b'id'].decode('utf-8'),
            'type': sensor[b'type'],
            'value': sensor[b'value']
        }
        result['sensors'].append(sensor_entry)
    
    return result
```

## C Decoding Example (msgpack-c)

```c
#include <msgpack.h>

msgpack_unpacked result;
msgpack_unpacked_init(&result);
msgpack_unpack_return ret = msgpack_unpack_next(&result, data, size, NULL);

if(ret == MSGPACK_UNPACK_SUCCESS) {
    msgpack_object obj = result.data;
    if(obj.type == MSGPACK_OBJECT_MAP) {
        // Access map fields: obj.via.map.ptr[i].key and obj.via.map.ptr[i].val
    }
}
msgpack_unpacked_destroy(&result);
```

## Important Notes

- Frames are sent continuously; clients must parse MessagePack objects sequentially from the TCP stream
- The timestamp is **milliseconds** since Unix epoch (not seconds)
- Sequence numbers increment with each frame sent
- Multiple frames may arrive in a single TCP read; parse MessagePack objects sequentially
- The protocol sends updates approximately every 300ms when sensor values change
- Timestamp represents milliseconds since January 1, 1970 00:00:00 UTC
- The schema is extensible: future sensors can use different types (int32, float64, string, etc.) without changing the wire format

## Connection Details

- **Default TCP port**: 9000 (configurable via `OPCUA_TCP_BRIDGE_PORT` environment variable)
- **Protocol**: Raw TCP stream with MessagePack-encoded frames
- **No handshake**: Connect and immediately start receiving MessagePack frames
- **Frame boundaries**: Clients must use a streaming MessagePack parser that can decode one complete MessagePack object at a time from the stream

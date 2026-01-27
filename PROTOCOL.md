# Binary Protocol Format for OPC UA TCP Bridge

## Frame Structure

- **Fixed-size frames**: 18 bytes per frame
- **Endianness**: All multi-byte integers are **big-endian** (network byte
  order)
- **Streaming**: Frames are sent continuously, one after another, with no
  delimiters

## Byte Layout

| Offset | Size    | Type   | Description                                                           |
| ------ | ------- | ------ | --------------------------------------------------------------------- |
| 0      | 1 byte  | uint8  | Protocol version (currently `0x01`)                                   |
| 1-8    | 8 bytes | uint64 | Sequence number (monotonically increasing, big-endian)                |
| 9-16   | 8 bytes | uint64 | Timestamp in milliseconds since Unix epoch (big-endian)               |
| 17     | 1 byte  | uint8  | Sensor flags byte (bits 0-3 represent Sensor1-Sensor4 boolean values) |

## Sensor Flags Byte (byte 17)

- **Bit 0**: Sensor1 value (1 = true, 0 = false)
- **Bit 1**: Sensor2 value (1 = true, 0 = false)
- **Bit 2**: Sensor3 value (1 = true, 0 = false)
- **Bit 3**: Sensor4 value (1 = true, 0 = false)
- **Bits 4-7**: Reserved (currently always 0)

## Example Frame Decoding

```
Raw bytes (hex): 01 00 00 00 00 00 00 00 48 00 00 01 9b ff 6e 6f 6d 0f

Byte 0:   0x01 = Protocol version 1
Bytes 1-8: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x48 = Sequence number 72
Bytes 9-16: 0x00 0x00 0x01 0x9b 0xff 0x6e 0x6f 0x6d = Timestamp 1769516986221 ms
Byte 17:   0x0f = Binary 00001111 = All 4 sensors are true
```

## Decoding Pseudocode

```python
def decode_frame(buffer):
    if len(buffer) < 18:
        raise ValueError("Frame too short")
    
    version = buffer[0]
    seq = int.from_bytes(buffer[1:9], byteorder='big')
    timestamp_ms = int.from_bytes(buffer[9:17], byteorder='big')
    flags = buffer[17]
    
    sensor1 = (flags & 0x01) != 0
    sensor2 = (flags & 0x02) != 0
    sensor3 = (flags & 0x04) != 0
    sensor4 = (flags & 0x08) != 0
    
    return {
        'version': version,
        'sequence': seq,
        'timestamp_ms': timestamp_ms,
        'sensor1': sensor1,
        'sensor2': sensor2,
        'sensor3': sensor3,
        'sensor4': sensor4
    }
```

## Big-Endian uint64 Decoding

For languages without built-in big-endian conversion:

```c
uint64_t value = ((uint64_t)buffer[0] << 56) |
                 ((uint64_t)buffer[1] << 48) |
                 ((uint64_t)buffer[2] << 40) |
                 ((uint64_t)buffer[3] << 32) |
                 ((uint64_t)buffer[4] << 24) |
                 ((uint64_t)buffer[5] << 16) |
                 ((uint64_t)buffer[6] << 8) |
                 (uint64_t)buffer[7];
```

## Important Notes

- Frames are sent continuously; read 18-byte chunks from the TCP stream
- The timestamp is **milliseconds** since Unix epoch (not seconds)
- Sequence numbers increment with each frame sent
- Multiple frames may arrive in a single TCP read; parse sequentially
- The protocol sends updates approximately every 300ms when sensor values change
- Timestamp represents milliseconds since January 1, 1970 00:00:00 UTC

## Connection Details

- **Default TCP port**: 9000 (configurable via `OPCUA_TCP_BRIDGE_PORT`
  environment variable)
- **Protocol**: Raw TCP stream
- **No handshake**: Connect and immediately start receiving frames

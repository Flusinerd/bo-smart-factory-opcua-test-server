package frame

import (
	"bytes"

	"github.com/vmihailenco/msgpack/v5"
)

const ProtocolVersion = 0x01

// Sensor is one entry in the sensors array (wire format).
type Sensor struct {
	ID       string
	TypeCode uint8
	Value    interface{}
}

// Encode builds one MessagePack frame matching PROTOCOL.md and the C/Qt bridge.
func Encode(sequence uint64, timestampMs uint64, sensors []Sensor) ([]byte, error) {
	m := map[string]interface{}{
		"version":     uint8(ProtocolVersion),
		"sequence":    sequence,
		"timestampMs": timestampMs,
		"sensors":     sensorMaps(sensors),
	}
	var buf bytes.Buffer
	enc := msgpack.NewEncoder(&buf)
	if err := enc.Encode(m); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func sensorMaps(sensors []Sensor) []map[string]interface{} {
	out := make([]map[string]interface{}, 0, len(sensors))
	for _, s := range sensors {
		out = append(out, map[string]interface{}{
			"id":    s.ID,
			"type":  s.TypeCode,
			"value": s.Value,
		})
	}
	return out
}

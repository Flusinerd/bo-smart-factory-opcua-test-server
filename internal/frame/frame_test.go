package frame

import (
	"testing"

	"github.com/vmihailenco/msgpack/v5"
)

func TestEncodeShape(t *testing.T) {
	b, err := Encode(1, 1700000000000, []Sensor{
		{ID: "Sensor1", TypeCode: 0, Value: true},
		{ID: "Sensor2", TypeCode: 0, Value: false},
	})
	if err != nil {
		t.Fatal(err)
	}
	var m map[string]interface{}
	if err := msgpack.Unmarshal(b, &m); err != nil {
		t.Fatal(err)
	}
	if m["version"].(uint8) != ProtocolVersion {
		t.Fatalf("version: %v", m["version"])
	}
	if m["sequence"].(uint64) != 1 {
		t.Fatalf("sequence: %v", m["sequence"])
	}
	sensors, ok := m["sensors"].([]interface{})
	if !ok || len(sensors) != 2 {
		t.Fatalf("sensors: %#v", m["sensors"])
	}
}

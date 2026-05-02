package nodeid

import (
	"testing"

	"github.com/gopcua/opcua/ua"
)

func TestParseQtNumeric(t *testing.T) {
	id, err := ParseQt("ns=2;i=42")
	if err != nil {
		t.Fatal(err)
	}
	if id.Namespace() != 2 || id.IntID() != 42 {
		t.Fatalf("got %v", id)
	}
}

func TestParseQtString(t *testing.T) {
	id, err := ParseQt("ns=2;s=Hello")
	if err != nil {
		t.Fatal(err)
	}
	if id.Namespace() != 2 || id.StringID() != "Hello" {
		t.Fatalf("got %v", id)
	}
}

func TestParseNative(t *testing.T) {
	id, err := ParseQt(ua.NewNumericNodeID(0, 85).String())
	if err != nil {
		t.Fatal(err)
	}
	if id.IntID() != 85 {
		t.Fatalf("got %v", id)
	}
}

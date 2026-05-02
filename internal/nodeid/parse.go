package nodeid

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/gopcua/opcua/ua"
)

var qtForm = regexp.MustCompile(`^ns=(\d+);([si])=(.+)$`)

// ParseQt parses node id strings as accepted by the Qt bridge: ns=(\d+);(s|i)=(.+)
func ParseQt(s string) (*ua.NodeID, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil, fmt.Errorf("empty node id")
	}
	// Prefer native parser when possible (covers more forms).
	if id, err := ua.ParseNodeID(s); err == nil {
		return id, nil
	}
	m := qtForm.FindStringSubmatch(s)
	if m == nil {
		return nil, fmt.Errorf("invalid node id (expected e.g. ns=2;i=123 or ns=2;s=Name)")
	}
	ns, err := strconv.ParseUint(m[1], 10, 16)
	if err != nil {
		return nil, err
	}
	switch m[2] {
	case "i":
		v, err := strconv.ParseUint(strings.TrimSpace(m[3]), 10, 32)
		if err != nil {
			return nil, err
		}
		return ua.NewNumericNodeID(uint16(ns), uint32(v)), nil
	case "s":
		return ua.NewStringNodeID(uint16(ns), m[3]), nil
	default:
		return nil, fmt.Errorf("unsupported id type %q", m[2])
	}
}

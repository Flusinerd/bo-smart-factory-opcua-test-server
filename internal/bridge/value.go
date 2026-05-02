package bridge

import (
	"github.com/gopcua/opcua/ua"
)

// dataValueToWire mirrors BridgeEngine.cpp value encoding for the MessagePack payload.
func dataValueToWire(dv *ua.DataValue) interface{} {
	if dv == nil || dv.Status != ua.StatusOK || dv.Value == nil {
		return false
	}
	v := dv.Value
	switch v.Type() {
	case ua.TypeIDBoolean:
		return v.Value().(bool)
	case ua.TypeIDSByte:
		return int32(v.Value().(int8))
	case ua.TypeIDByte:
		return int32(v.Value().(uint8))
	case ua.TypeIDInt16:
		return int32(v.Value().(int16))
	case ua.TypeIDUint16:
		return int32(v.Value().(uint16))
	case ua.TypeIDInt32:
		return v.Value().(int32)
	case ua.TypeIDUint32:
		return int64(v.Value().(uint32))
	case ua.TypeIDInt64:
		return v.Value().(int64)
	case ua.TypeIDUint64:
		return int64(v.Value().(uint64))
	case ua.TypeIDFloat:
		return v.Value().(float32)
	case ua.TypeIDDouble:
		return v.Value().(float64)
	case ua.TypeIDString:
		return v.Value().(string)
	case ua.TypeIDByteString:
		return string(v.Value().([]byte))
	default:
		return false
	}
}

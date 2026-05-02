package config

import (
	"encoding/json"
	"os"
)

// Mapping matches the Qt GUI JSON (MainWindow / SensorMapping).
type Mapping struct {
	MessagePackID string `json:"messagePackId"`
	NodeIDString  string `json:"nodeIdString"`
	TypeCode      uint8  `json:"typeCode"`
	Enabled       bool   `json:"enabled"`
}

// Config is persisted JSON compatible with the Qt bridge GUI.
type Config struct {
	EndpointURL   string    `json:"endpointUrl"`
	NamespaceURI  string    `json:"namespaceUri"`
	TCPPort       int       `json:"tcpPort"`
	Mappings      []Mapping `json:"mappings"`
}

func Default() Config {
	return Config{
		EndpointURL:  "opc.tcp://localhost:4840",
		NamespaceURI: "urn:binary-sensors-demo",
		TCPPort:      9000,
		Mappings:     nil,
	}
}

func LoadFile(path string) (Config, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return Config{}, err
	}
	var c Config
	if err := json.Unmarshal(b, &c); err != nil {
		return Config{}, err
	}
	return c, nil
}

func SaveFile(path string, c Config) error {
	b, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, b, 0o644)
}

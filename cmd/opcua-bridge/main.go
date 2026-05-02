package main

import (
	"log"
	"net/http"
	"os"

	"opcua-server-hs-bochum/internal/api"
	"opcua-server-hs-bochum/internal/bridge"
	"opcua-server-hs-bochum/internal/config"
	"opcua-server-hs-bochum/internal/web"
)

func main() {
	cfg := config.Default()
	configPath := os.Getenv("CONFIG_PATH")
	if configPath != "" {
		if c, err := config.LoadFile(configPath); err == nil {
			cfg = c
		} else {
			log.Printf("config: could not load %s: %v (using defaults)", configPath, err)
		}
	}
	if p, ok := bridge.TCPPortFromEnv(); ok {
		cfg.TCPPort = p
	}
	if v := os.Getenv("OPCUA_ENDPOINT"); v != "" {
		cfg.EndpointURL = v
	}
	if v := os.Getenv("OPCUA_NAMESPACE_URI"); v != "" {
		cfg.NamespaceURI = v
	}

	svc := bridge.NewService(cfg, configPath)

	httpAddr := os.Getenv("HTTP_ADDR")
	if httpAddr == "" {
		httpAddr = ":8080"
	}

	mux := http.NewServeMux()
	h := &api.Handler{Svc: svc}
	h.Register(mux)
	mux.Handle("/", web.Handler())

	log.Printf("HTTP UI listening on %s", httpAddr)
	if err := http.ListenAndServe(httpAddr, mux); err != nil {
		log.Fatal(err)
	}
}

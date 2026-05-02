package api

import (
	"context"
	"encoding/json"
	"log"
	"net/http"
	"time"

	"opcua-server-hs-bochum/internal/bridge"
	"opcua-server-hs-bochum/internal/config"
)

const jsonTimeout = 30 * time.Second

// Handler serves REST endpoints for the bridge.
type Handler struct {
	Svc *bridge.Service
}

func (h *Handler) withJSON(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, PUT, POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next(w, r)
	}
}

func (h *Handler) Register(mux *http.ServeMux) {
	mux.HandleFunc("/api/health", h.withJSON(h.health))
	mux.HandleFunc("/api/config", h.withJSON(h.configHandler))
	mux.HandleFunc("/api/opcua/connect", h.withJSON(h.connect))
	mux.HandleFunc("/api/opcua/disconnect", h.withJSON(h.disconnect))
	mux.HandleFunc("/api/browse", h.withJSON(h.browse))
	mux.HandleFunc("/api/bridge/start", h.withJSON(h.bridgeStart))
	mux.HandleFunc("/api/bridge/stop", h.withJSON(h.bridgeStop))
	mux.HandleFunc("/api/status", h.withJSON(h.status))
}

func (h *Handler) health(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, `{"error":"method not allowed"}`, http.StatusMethodNotAllowed)
		return
	}
	_, _ = w.Write([]byte(`{"ok":true}`))
}

func (h *Handler) configHandler(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		c := h.Svc.GetConfig()
		_ = json.NewEncoder(w).Encode(c)
	case http.MethodPut:
		var c config.Config
		if err := json.NewDecoder(r.Body).Decode(&c); err != nil {
			http.Error(w, jsonErr(err.Error()), http.StatusBadRequest)
			return
		}
		if err := h.Svc.SetConfig(c); err != nil {
			http.Error(w, jsonErr(err.Error()), http.StatusInternalServerError)
			return
		}
		_ = json.NewEncoder(w).Encode(h.Svc.GetConfig())
	default:
		http.Error(w, jsonErr("method not allowed"), http.StatusMethodNotAllowed)
	}
}

func (h *Handler) connect(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, jsonErr("method not allowed"), http.StatusMethodNotAllowed)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), jsonTimeout)
	defer cancel()
	if err := h.Svc.Connect(ctx); err != nil {
		http.Error(w, jsonErr(err.Error()), http.StatusBadRequest)
		return
	}
	_ = json.NewEncoder(w).Encode(h.Svc.Status())
}

func (h *Handler) disconnect(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, jsonErr("method not allowed"), http.StatusMethodNotAllowed)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), jsonTimeout)
	defer cancel()
	if err := h.Svc.Disconnect(ctx); err != nil {
		http.Error(w, jsonErr(err.Error()), http.StatusBadRequest)
		return
	}
	_ = json.NewEncoder(w).Encode(h.Svc.Status())
}

type browseBody struct {
	NodeID string `json:"nodeId"`
}

func (h *Handler) browse(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, jsonErr("method not allowed"), http.StatusMethodNotAllowed)
		return
	}
	var body browseBody
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		http.Error(w, jsonErr(err.Error()), http.StatusBadRequest)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), jsonTimeout)
	defer cancel()
	refs, err := h.Svc.Browse(ctx, body.NodeID)
	if err != nil {
		http.Error(w, jsonErr(err.Error()), http.StatusBadRequest)
		return
	}
	_ = json.NewEncoder(w).Encode(map[string]interface{}{"references": refs})
}

func (h *Handler) bridgeStart(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, jsonErr("method not allowed"), http.StatusMethodNotAllowed)
		return
	}
	if err := h.Svc.StartBridge(); err != nil {
		http.Error(w, jsonErr(err.Error()), http.StatusBadRequest)
		return
	}
	_ = json.NewEncoder(w).Encode(h.Svc.Status())
}

func (h *Handler) bridgeStop(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, jsonErr("method not allowed"), http.StatusMethodNotAllowed)
		return
	}
	if err := h.Svc.StopBridge(); err != nil {
		http.Error(w, jsonErr(err.Error()), http.StatusBadRequest)
		return
	}
	_ = json.NewEncoder(w).Encode(h.Svc.Status())
}

func (h *Handler) status(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, jsonErr("method not allowed"), http.StatusMethodNotAllowed)
		return
	}
	_ = json.NewEncoder(w).Encode(h.Svc.Status())
}

func jsonErr(msg string) string {
	b, err := json.Marshal(map[string]string{"error": msg})
	if err != nil {
		log.Printf("json marshal: %v", err)
		return `{"error":"internal"}`
	}
	return string(b)
}

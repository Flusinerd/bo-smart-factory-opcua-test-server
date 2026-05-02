package bridge

import (
	"context"
	"errors"
	"net"
	"os"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"opcua-server-hs-bochum/internal/config"
	"opcua-server-hs-bochum/internal/frame"
	"opcua-server-hs-bochum/internal/nodeid"

	"github.com/gopcua/opcua"
	"github.com/gopcua/opcua/id"
	"github.com/gopcua/opcua/ua"
)

const updateInterval = 300 * time.Millisecond

// BrowseRef is one child reference for the web UI tree.
type BrowseRef struct {
	NodeID      string `json:"nodeId"`
	BrowseName  string `json:"browseName"`
	DisplayName string `json:"displayName"`
	NodeClass   uint32 `json:"nodeClass"`
}

// Status is returned by GET /api/status.
type Status struct {
	OpcUAConnected bool   `json:"opcuaConnected"`
	BridgeRunning  bool   `json:"bridgeRunning"`
	TCPPort        int    `json:"tcpPort"`
	ClientCount    int    `json:"clientCount"`
	LastError      string `json:"lastError,omitempty"`
	NamespaceIndex uint16 `json:"namespaceIndex,omitempty"`
}

type resolvedMapping struct {
	msgID    string
	typeCode uint8
	nid      *ua.NodeID
}

// Service orchestrates OPC UA, TCP streaming, and config.
type Service struct {
	mu sync.RWMutex

	cfg        config.Config
	configPath string

	client       *opcua.Client
	namespaceIdx uint16
	opcConnected bool
	lastErr      string

	bridgeRunning bool
	listener      net.Listener
	clients       []net.Conn
	bridgeStop    chan struct{}
	bridgeWG      sync.WaitGroup

	seq atomic.Uint64
}

func NewService(cfg config.Config, configPath string) *Service {
	return &Service{cfg: cfg, configPath: configPath}
}

func (s *Service) GetConfig() config.Config {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.cfg
}

func (s *Service) SetConfig(c config.Config) error {
	s.mu.Lock()
	s.cfg = c
	path := s.configPath
	s.mu.Unlock()
	if path != "" {
		return config.SaveFile(path, c)
	}
	return nil
}

func (s *Service) Status() Status {
	s.mu.RLock()
	defer s.mu.RUnlock()
	st := Status{
		OpcUAConnected: s.opcConnected,
		BridgeRunning:  s.bridgeRunning,
		TCPPort:        s.cfg.TCPPort,
		ClientCount:    len(s.clients),
		LastError:      s.lastErr,
		NamespaceIndex: s.namespaceIdx,
	}
	return st
}

func (s *Service) setErr(msg string) {
	s.mu.Lock()
	s.lastErr = msg
	s.mu.Unlock()
}

func (s *Service) clearErr() {
	s.mu.Lock()
	s.lastErr = ""
	s.mu.Unlock()
}

// Connect establishes OPC UA session and resolves namespace URI.
func (s *Service) Connect(ctx context.Context) error {
	s.mu.Lock()
	if s.bridgeRunning {
		s.mu.Unlock()
		return errors.New("stop the bridge before reconnecting")
	}
	if s.client != nil {
		_ = s.client.Close(ctx)
		s.client = nil
	}
	s.opcConnected = false
	s.mu.Unlock()

	s.clearErr()

	c, err := opcua.NewClient(s.GetConfig().EndpointURL)
	if err != nil {
		s.setErr(err.Error())
		return err
	}
	if err := c.Connect(ctx); err != nil {
		s.setErr(err.Error())
		return err
	}

	ns, err := c.FindNamespace(ctx, s.GetConfig().NamespaceURI)
	if err != nil {
		_ = c.Close(ctx)
		s.setErr(err.Error())
		return err
	}

	s.mu.Lock()
	s.client = c
	s.namespaceIdx = ns
	s.opcConnected = true
	s.mu.Unlock()
	return nil
}

// Disconnect closes OPC UA session.
func (s *Service) Disconnect(ctx context.Context) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.bridgeRunning {
		return errors.New("stop the bridge first")
	}
	if s.client != nil {
		err := s.client.Close(ctx)
		s.client = nil
		s.opcConnected = false
		return err
	}
	s.opcConnected = false
	return nil
}

// Browse lists direct hierarchical children (objects and variables).
func (s *Service) Browse(ctx context.Context, nodeIDStr string) ([]BrowseRef, error) {
	if nodeIDStr == "" {
		nodeIDStr = "ns=0;i=85"
	}
	s.mu.RLock()
	c := s.client
	connected := s.opcConnected
	s.mu.RUnlock()
	if !connected || c == nil {
		return nil, errors.New("not connected")
	}
	nid, err := nodeid.ParseQt(nodeIDStr)
	if err != nil {
		return nil, err
	}
	node := c.Node(nid)
	refs, err := node.References(ctx, id.HierarchicalReferences, ua.BrowseDirectionForward,
		ua.NodeClassVariable|ua.NodeClassObject, true)
	if err != nil {
		return nil, err
	}
	out := make([]BrowseRef, 0, len(refs))
	for _, r := range refs {
		if !r.IsForward {
			continue
		}
		if r.NodeID == nil || r.NodeID.NodeID == nil {
			continue
		}
		br := BrowseRef{
			NodeID:    r.NodeID.NodeID.String(),
			NodeClass: uint32(r.NodeClass),
		}
		if r.BrowseName != nil {
			br.BrowseName = r.BrowseName.Name
		}
		if r.DisplayName != nil {
			br.DisplayName = r.DisplayName.Text
		}
		if br.DisplayName == "" {
			br.DisplayName = br.BrowseName
		}
		if br.BrowseName == "" {
			br.BrowseName = br.NodeID
		}
		out = append(out, br)
	}
	return out, nil
}

func (s *Service) resolveMappings() ([]resolvedMapping, error) {
	cfg := s.GetConfig()
	var out []resolvedMapping
	for _, m := range cfg.Mappings {
		if !m.Enabled || m.NodeIDString == "" {
			continue
		}
		nid, err := nodeid.ParseQt(m.NodeIDString)
		if err != nil {
			return nil, err
		}
		msgID := m.MessagePackID
		if msgID == "" {
			msgID = m.NodeIDString
		}
		out = append(out, resolvedMapping{
			msgID:    msgID,
			typeCode: m.TypeCode,
			nid:      nid,
		})
	}
	return out, nil
}

func (s *Service) readFrame(ctx context.Context) ([]byte, error) {
	s.mu.RLock()
	c := s.client
	s.mu.RUnlock()
	if c == nil {
		return nil, errors.New("no client")
	}
	mappings, err := s.resolveMappings()
	if err != nil {
		return nil, err
	}
	sensors := make([]frame.Sensor, 0, len(mappings))
	for _, m := range mappings {
		req := &ua.ReadRequest{
			NodesToRead: []*ua.ReadValueID{{
				NodeID:      m.nid,
				AttributeID: ua.AttributeIDValue,
			}},
		}
		res, err := c.Read(ctx, req)
		if err != nil || len(res.Results) == 0 {
			sensors = append(sensors, frame.Sensor{ID: m.msgID, TypeCode: m.typeCode, Value: false})
			continue
		}
		val := dataValueToWire(res.Results[0])
		sensors = append(sensors, frame.Sensor{ID: m.msgID, TypeCode: m.typeCode, Value: val})
	}
	seq := s.seq.Add(1)
	ts := uint64(time.Now().UnixMilli())
	return frame.Encode(seq, ts, sensors)
}

func (s *Service) broadcast(payload []byte) {
	s.mu.Lock()
	defer s.mu.Unlock()
	alive := s.clients[:0]
	for _, conn := range s.clients {
		if _, err := conn.Write(payload); err != nil {
			_ = conn.Close()
			continue
		}
		alive = append(alive, conn)
	}
	s.clients = alive
}

// StartBridge listens on TCP and broadcasts MessagePack frames.
func (s *Service) StartBridge() error {
	s.mu.Lock()
	if s.bridgeRunning {
		s.mu.Unlock()
		return errors.New("bridge already running")
	}
	if !s.opcConnected || s.client == nil {
		s.mu.Unlock()
		return errors.New("OPC UA not connected")
	}
	port := s.cfg.TCPPort
	if port <= 0 || port > 65535 {
		s.mu.Unlock()
		return errors.New("invalid tcp port")
	}
	ln, err := net.Listen("tcp", ":"+strconv.Itoa(port))
	if err != nil {
		s.mu.Unlock()
		s.setErr(err.Error())
		return err
	}
	s.listener = ln
	s.bridgeRunning = true
	s.clients = nil
	s.bridgeStop = make(chan struct{})
	s.mu.Unlock()
	s.clearErr()

	s.bridgeWG.Add(2)
	go s.acceptLoop(ln)
	go s.tickLoop()
	return nil
}

func (s *Service) acceptLoop(ln net.Listener) {
	defer s.bridgeWG.Done()
	for {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		s.mu.Lock()
		s.clients = append(s.clients, conn)
		s.mu.Unlock()
		go s.watchDisconnect(conn)
	}
}

func (s *Service) watchDisconnect(c net.Conn) {
	buf := make([]byte, 1)
	for {
		_, err := c.Read(buf)
		if err != nil {
			s.mu.Lock()
			for i, cc := range s.clients {
				if cc == c {
					s.clients = append(s.clients[:i], s.clients[i+1:]...)
					break
				}
			}
			s.mu.Unlock()
			_ = c.Close()
			return
		}
	}
}

func (s *Service) tickLoop() {
	defer s.bridgeWG.Done()
	s.mu.RLock()
	stop := s.bridgeStop
	s.mu.RUnlock()
	t := time.NewTicker(updateInterval)
	defer t.Stop()
	ctx := context.Background()
	for {
		select {
		case <-stop:
			return
		case <-t.C:
			s.mu.RLock()
			run := s.bridgeRunning && s.opcConnected
			cl := s.client
			s.mu.RUnlock()
			if !run || cl == nil {
				continue
			}
			st := cl.State()
			if st != opcua.Connected {
				s.setErr("OPC UA connection lost")
				s.mu.Lock()
				s.opcConnected = false
				s.mu.Unlock()
				go func() { _ = s.StopBridge() }()
				return
			}
			payload, err := s.readFrame(ctx)
			if err != nil {
				s.setErr(err.Error())
				continue
			}
			s.broadcast(payload)
		}
	}
}

// StopBridge stops TCP server and broadcast loop.
func (s *Service) StopBridge() error {
	s.mu.Lock()
	if !s.bridgeRunning {
		s.mu.Unlock()
		return nil
	}
	ch := s.bridgeStop
	ln := s.listener
	s.bridgeStop = nil
	s.listener = nil
	s.bridgeRunning = false
	clients := s.clients
	s.clients = nil
	s.mu.Unlock()

	if ln != nil {
		_ = ln.Close()
	}
	if ch != nil {
		close(ch)
	}
	for _, c := range clients {
		_ = c.Close()
	}
	s.bridgeWG.Wait()
	return nil
}

// TCPPortFromEnv overrides config TCP port when OPCUA_TCP_BRIDGE_PORT is set.
func TCPPortFromEnv() (int, bool) {
	p := os.Getenv("OPCUA_TCP_BRIDGE_PORT")
	if p == "" {
		return 0, false
	}
	n, err := strconv.Atoi(p)
	if err != nil || n <= 0 || n > 65535 {
		return 0, false
	}
	return n, true
}

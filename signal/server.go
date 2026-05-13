// Mine Bombers WebRTC signaling server.
//
// Pure relay: rooms hold up to 4 slots; clients exchange SDP offers/answers
// and ICE candidates over WebSocket. No game traffic flows through here.
//
// Build (cross-compile from Windows to linux/amd64):
//   $env:GOOS='linux'; $env:GOARCH='amd64'; go build -o mb-signal ./server.go
//
// Run:
//   ./mb-signal              # listens on :7000
//   PORT=8080 ./mb-signal    # override port
//
// Protocol (JSON over WS), same as the prior Node version:
//   client -> server:
//     {type:"host", room}                      create room, become slot 0
//     {type:"join", room}                      join, assigned a slot
//     {type:"offer"|"answer"|"ice", to, sdp|cand}   forwarded to peer
//   server -> client:
//     {type:"joined", slot, peers:[...]}
//     {type:"peer-join", slot}
//     {type:"peer-leave", slot}
//     {type:"offer"|"answer"|"ice", from, sdp|cand}
//     {type:"error", reason}

package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"strings"
	"sync"

	"github.com/gorilla/websocket"
)

const (
	maxSlots       = 4
	maxRoomNameLen = 64
	maxMessageSize = 64 * 1024 // 64 KB hard cap per WS frame (SDP blobs are <8 KB).
)

type client struct {
	conn   *websocket.Conn
	send   chan []byte
	room   *room
	roomID string
	slot   int
}

type room struct {
	slots [maxSlots]*client
}

type msg struct {
	Type string `json:"type"`
	Room string `json:"room,omitempty"`
	// Slot/From/Peers: never omitempty -- slot 0 (host) and empty peer lists
	// are real values, and omitempty would drop them, breaking JS clients.
	Slot   int             `json:"slot"`
	From   int             `json:"from"`
	To     *int            `json:"to,omitempty"`
	Peers  []int           `json:"peers"`
	Reason string          `json:"reason,omitempty"`
	SDP    string          `json:"sdp,omitempty"`
	Cand   json.RawMessage `json:"cand,omitempty"`
}

var (
	rooms    = map[string]*room{}
	roomsMu  sync.Mutex
	// Origins allowed to open a signaling WS. Behind nginx the Origin header is
	// preserved, so this still works. localhost entries cover local dev.
	allowedOrigins = map[string]bool{
		"https://quad.fi":       true,
		"http://quad.fi":        true,
		"http://localhost:8080": true,
		"http://127.0.0.1:8080": true,
	}
	upgrader = websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool {
			o := r.Header.Get("Origin")
			ok := allowedOrigins[o] ||
				strings.HasPrefix(o, "http://localhost:") ||
				strings.HasPrefix(o, "http://127.0.0.1:") ||
				o == "http://localhost" || o == "http://127.0.0.1"
			if !ok {
				log.Printf("[ws] origin rejected: %q", o)
			}
			return ok
		},
	}
)

func send(c *client, m msg) {
	b, err := json.Marshal(m)
	if err != nil {
		return
	}
	select {
	case c.send <- b:
	default:
		log.Printf("[ws] dropping msg to slot %d (send queue full)", c.slot)
	}
}

func freeSlot(r *room) int {
	for i := 0; i < maxSlots; i++ {
		if r.slots[i] == nil {
			return i
		}
	}
	return -1
}

func leave(c *client) {
	roomsMu.Lock()
	defer roomsMu.Unlock()
	r := c.room
	if r == nil {
		return
	}
	slot := c.slot
	r.slots[slot] = nil
	for _, peer := range r.slots {
		if peer != nil {
			send(peer, msg{Type: "peer-leave", Slot: slot})
		}
	}
	empty := true
	for _, s := range r.slots {
		if s != nil {
			empty = false
			break
		}
	}
	if empty {
		delete(rooms, c.roomID)
	}
	c.room = nil
}

func handle(c *client, m msg) {
	// Room name bound: prevents map-key bloat from malicious clients.
	if (m.Type == "host" || m.Type == "join") && len(m.Room) > maxRoomNameLen {
		send(c, msg{Type: "error", Reason: "room name too long"})
		return
	}
	switch m.Type {
	case "host":
		roomsMu.Lock()
		if _, exists := rooms[m.Room]; exists {
			roomsMu.Unlock()
			send(c, msg{Type: "error", Reason: "room exists"})
			return
		}
		r := &room{}
		r.slots[0] = c
		c.room, c.roomID, c.slot = r, m.Room, 0
		rooms[m.Room] = r
		roomsMu.Unlock()
		log.Printf("[ws] host room=%s", m.Room)
		send(c, msg{Type: "joined", Slot: 0, Peers: []int{}})

	case "join":
		roomsMu.Lock()
		r, ok := rooms[m.Room]
		if !ok {
			roomsMu.Unlock()
			log.Printf("[ws] join FAIL no room=%s", m.Room)
			send(c, msg{Type: "error", Reason: "no such room"})
			return
		}
		slot := freeSlot(r)
		if slot < 0 {
			roomsMu.Unlock()
			send(c, msg{Type: "error", Reason: "room full"})
			return
		}
		r.slots[slot] = c
		c.room, c.roomID, c.slot = r, m.Room, slot
		peers := []int{}
		for i, s := range r.slots {
			if s != nil && i != slot {
				peers = append(peers, i)
			}
		}
		// Snapshot peer pointers before releasing lock for fan-out.
		others := make([]*client, 0, maxSlots-1)
		for i, s := range r.slots {
			if s != nil && i != slot {
				others = append(others, s)
			}
		}
		roomsMu.Unlock()
		log.Printf("[ws] join room=%s -> slot %d, peers=%v", m.Room, slot, peers)
		send(c, msg{Type: "joined", Slot: slot, Peers: peers})
		for _, peer := range others {
			send(peer, msg{Type: "peer-join", Slot: slot})
		}

	case "offer", "answer", "ice":
		if c.room == nil || m.To == nil {
			return
		}
		roomsMu.Lock()
		target := c.room.slots[*m.To]
		roomsMu.Unlock()
		if target == nil {
			log.Printf("[ws] relay %s target slot %d empty", m.Type, *m.To)
			return
		}
		if m.Type != "ice" {
			log.Printf("[ws] relay %s %d -> %d", m.Type, c.slot, *m.To)
		}
		out := msg{Type: m.Type, From: c.slot, SDP: m.SDP, Cand: m.Cand}
		send(target, out)
	}
}

func reader(c *client) {
	defer func() {
		leave(c)
		c.conn.Close()
		close(c.send)
	}()
	for {
		_, data, err := c.conn.ReadMessage()
		if err != nil {
			return
		}
		var m msg
		if err := json.Unmarshal(data, &m); err != nil {
			continue
		}
		handle(c, m)
	}
}

func writer(c *client) {
	for b := range c.send {
		if err := c.conn.WriteMessage(websocket.TextMessage, b); err != nil {
			return
		}
	}
}

func wsHandler(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("[ws] upgrade fail: %v", err)
		return
	}
	conn.SetReadLimit(maxMessageSize)
	c := &client{conn: conn, send: make(chan []byte, 32), slot: -1}
	log.Printf("[ws] connect from %s", r.RemoteAddr)
	go writer(c)
	reader(c)
}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "7000"
	}
	http.HandleFunc("/", wsHandler)
	log.Printf("signal server listening on :%s", port)
	if err := http.ListenAndServe(":"+port, nil); err != nil {
		log.Fatal(err)
	}
}

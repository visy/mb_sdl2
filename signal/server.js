// Minimal WebRTC signaling server for Mine Bombers.
// Run: node server.js [port]
// Deploy on Linode (quad.fi): put behind Caddy/nginx with TLS, e.g.
//   signal.quad.fi -> reverse_proxy localhost:7000
//
// Protocol (JSON over WebSocket):
//   client -> server:
//     {type:'host', room}                       create room, become slot 0
//     {type:'join', room}                       join existing, assigned slot
//     {type:'offer'|'answer'|'ice', to, sdp|cand}   forwarded to peer
//   server -> client:
//     {type:'joined', slot, peers:[...]}        on host/join success
//     {type:'peer-join', slot}                  new peer joined room
//     {type:'peer-leave', slot}                 peer disconnected
//     {type:'offer'|'answer'|'ice', from, sdp|cand}  relayed
//     {type:'error', reason}

const { WebSocketServer } = require('ws');

const PORT = parseInt(process.argv[2] || process.env.PORT || '7000', 10);
const MAX_SLOTS = 4;

/** @type {Map<string, {slots: (WebSocket|null)[]}>} */
const rooms = new Map();

const wss = new WebSocketServer({ port: PORT });
console.log(`signal server listening on :${PORT}`);

function send(ws, obj) {
  if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
}

function freeSlot(room) {
  for (let i = 0; i < MAX_SLOTS; i++) if (!room.slots[i]) return i;
  return -1;
}

function leave(ws) {
  const room = ws._room;
  if (!room) return;
  const slot = ws._slot;
  room.slots[slot] = null;
  for (const peer of room.slots) {
    if (peer) send(peer, { type: 'peer-leave', slot });
  }
  // Drop empty rooms.
  if (room.slots.every(s => !s)) rooms.delete(ws._roomId);
  ws._room = null;
}

wss.on('connection', (ws, req) => {
  ws._room = null;
  console.log('[ws] connect from', req.socket.remoteAddress);

  ws.on('message', (raw) => {
    let msg;
    try { msg = JSON.parse(raw.toString()); } catch { return; }

    if (msg.type === 'host') {
      if (rooms.has(msg.room)) return send(ws, { type: 'error', reason: 'room exists' });
      const room = { slots: new Array(MAX_SLOTS).fill(null) };
      room.slots[0] = ws;
      ws._room = room; ws._roomId = msg.room; ws._slot = 0;
      rooms.set(msg.room, room);
      console.log(`[ws] host room=${msg.room}`);
      send(ws, { type: 'joined', slot: 0, peers: [] });
      return;
    }

    if (msg.type === 'join') {
      const room = rooms.get(msg.room);
      if (!room) { console.log(`[ws] join FAIL no room=${msg.room}`); return send(ws, { type: 'error', reason: 'no such room' }); }
      const slot = freeSlot(room);
      if (slot < 0) return send(ws, { type: 'error', reason: 'room full' });
      room.slots[slot] = ws;
      ws._room = room; ws._roomId = msg.room; ws._slot = slot;
      const occupied = room.slots
        .map((s, i) => s && i !== slot ? i : -1)
        .filter(i => i >= 0);
      console.log(`[ws] join room=${msg.room} -> slot ${slot}, peers=${JSON.stringify(occupied)}`);
      send(ws, { type: 'joined', slot, peers: occupied });
      for (const peer of room.slots) {
        if (peer && peer !== ws) send(peer, { type: 'peer-join', slot });
      }
      return;
    }

    // Relay: offer / answer / ice.
    if (msg.type === 'offer' || msg.type === 'answer' || msg.type === 'ice') {
      const room = ws._room;
      if (!room) { console.log(`[ws] relay ${msg.type} but sender has no room`); return; }
      const target = room.slots[msg.to];
      if (!target) { console.log(`[ws] relay ${msg.type} target slot ${msg.to} empty`); return; }
      if (msg.type !== 'ice') console.log(`[ws] relay ${msg.type} ${ws._slot} -> ${msg.to}`);
      send(target, { ...msg, from: ws._slot, to: undefined });
      return;
    }
  });

  ws.on('close', () => leave(ws));
  ws.on('error', () => leave(ws));
});

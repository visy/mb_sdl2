// WebRTC + signaling bridge for Mine Bombers WASM netplay.
//
// Exposes window.MBNet, called from C via EM_JS in net_web.c (Phase 2).
//
// Wire model: host opens DataChannels (reliable+ordered) to each client (star
// topology mirroring ENet host/peer). Signaling done over WebSocket to
// SIGNAL_URL — server only relays SDP + ICE; no game traffic flows through it.

(function () {
  // Local dev: hostname is localhost OR 127.0.0.1 OR any private LAN address.
  // Prod: anything else points at signal.quad.fi.
  const host = location.hostname;
  const isLocal = host === 'localhost' || host === '127.0.0.1' ||
                  /^192\.168\./.test(host) || /^10\./.test(host) ||
                  /^172\.(1[6-9]|2\d|3[01])\./.test(host);
  const SIGNAL_URL = isLocal
    ? `ws://${host === 'localhost' ? 'localhost' : host}:7000`
    : 'wss://signal.quad.fi/ws';

  // RTC config: STUN public, TURN should be added when coturn deployed on quad.fi.
  const RTC_CONFIG = {
    iceServers: [
      { urls: 'stun:stun.l.google.com:19302' },
      // { urls: 'turn:turn.quad.fi:3478', username: 'mb', credential: 'CHANGE_ME' },
    ],
  };

  // Queue of received packets (Uint8Array) per peer slot, drained by net_web.c.
  // Slot 0 = self. Slot 1..3 = remote peers.
  const inbox = [[], [], [], []];

  // RTCPeerConnection + DataChannel per remote slot. peers[slot] = {pc, dc}.
  const peers = [null, null, null, null];

  let ws = null;
  let isHost = false;
  let roomId = null;
  let localSlot = 0;

  function setStatus(s) {
    const el = document.getElementById('netstatus');
    if (el) el.textContent = s;
    console.log('[net]', s);
  }

  function wsOpen() {
    return new Promise((resolve, reject) => {
      if (ws && ws.readyState === 1) return resolve();
      ws = new WebSocket(SIGNAL_URL);
      ws.onopen = () => resolve();
      ws.onerror = (e) => reject(e);
      ws.onmessage = (ev) => onSignal(JSON.parse(ev.data));
      ws.onclose = () => setStatus('signal disconnected');
    });
  }

  async function onSignal(msg) {
    // msg shapes: {type:'joined', slot, peers}, {type:'peer-join', slot},
    // {type:'offer', from, sdp}, {type:'answer', from, sdp}, {type:'ice', from, cand}
    switch (msg.type) {
      case 'joined':
        localSlot = msg.slot;
        setStatus(`joined room ${roomId} as slot ${localSlot}`);
        if (isHost) {
          // Host creates offers to each existing/future client.
          for (const slot of msg.peers) await openOfferTo(slot);
        }
        break;
      case 'peer-join':
        if (isHost) await openOfferTo(msg.slot);
        break;
      case 'offer': {
        const pc = ensurePeer(msg.from, /*createDc*/ false);
        await pc.setRemoteDescription({ type: 'offer', sdp: msg.sdp });
        const ans = await pc.createAnswer();
        await pc.setLocalDescription(ans);
        ws.send(JSON.stringify({ type: 'answer', to: msg.from, sdp: ans.sdp }));
        break;
      }
      case 'answer': {
        const pc = peers[msg.from]?.pc;
        if (pc) await pc.setRemoteDescription({ type: 'answer', sdp: msg.sdp });
        break;
      }
      case 'ice': {
        const pc = peers[msg.from]?.pc;
        if (pc && msg.cand) await pc.addIceCandidate(msg.cand).catch(() => {});
        break;
      }
    }
  }

  function ensurePeer(slot, createDc) {
    if (peers[slot]) return peers[slot].pc;
    const pc = new RTCPeerConnection(RTC_CONFIG);
    pc.onicecandidate = (e) => {
      if (e.candidate) ws.send(JSON.stringify({ type: 'ice', to: slot, cand: e.candidate }));
    };
    pc.ondatachannel = (e) => attachDc(slot, e.channel);
    let dc = null;
    if (createDc) {
      dc = pc.createDataChannel('mb', { ordered: true });
      attachDc(slot, dc);
    }
    peers[slot] = { pc, dc };
    return pc;
  }

  function attachDc(slot, dc) {
    dc.binaryType = 'arraybuffer';
    dc.onopen = () => setStatus(`peer ${slot} open`);
    dc.onclose = () => setStatus(`peer ${slot} closed`);
    dc.onmessage = (ev) => {
      inbox[slot].push(new Uint8Array(ev.data));
    };
    peers[slot].dc = dc;
  }

  async function openOfferTo(slot) {
    const pc = ensurePeer(slot, /*createDc*/ true);
    const off = await pc.createOffer();
    await pc.setLocalDescription(off);
    ws.send(JSON.stringify({ type: 'offer', to: slot, sdp: off.sdp }));
  }

  // ---- API exposed to WASM (net_web.c calls these via EM_JS) ----
  window.MBNet = {
    async host(room) {
      isHost = true; roomId = room; localSlot = 0;
      await wsOpen();
      ws.send(JSON.stringify({ type: 'host', room }));
    },
    async join(room) {
      isHost = false; roomId = room;
      await wsOpen();
      ws.send(JSON.stringify({ type: 'join', room }));
    },
    // Returns 0 if no data, else slot index; writes bytes into HEAPU8 at ptr.
    // Caller passes buffer + max len; we drain one packet at a time.
    poll(ptr, maxLen) {
      for (let s = 0; s < inbox.length; s++) {
        if (inbox[s].length === 0) continue;
        const pkt = inbox[s].shift();
        if (pkt.length > maxLen) return -1;
        HEAPU8.set(pkt, ptr);
        return (s << 16) | pkt.length;
      }
      return 0;
    },
    sendTo(slot, ptr, len) {
      const dc = peers[slot]?.dc;
      if (!dc || dc.readyState !== 'open') return 0;
      dc.send(HEAPU8.subarray(ptr, ptr + len));
      return 1;
    },
    broadcast(ptr, len) {
      const view = HEAPU8.subarray(ptr, ptr + len);
      let n = 0;
      for (let s = 0; s < peers.length; s++) {
        const dc = peers[s]?.dc;
        if (dc && dc.readyState === 'open') { dc.send(view); n++; }
      }
      return n;
    },
    localSlot() { return localSlot; },
    isConnected(slot) { return peers[slot]?.dc?.readyState === 'open' ? 1 : 0; },
    setStatus,
  };

  // Optional: simple lobby UI for manual testing.
  document.getElementById('host')?.addEventListener('click', () => {
    window.MBNet.host(document.getElementById('room').value);
  });
  document.getElementById('join')?.addEventListener('click', () => {
    window.MBNet.join(document.getElementById('room').value);
  });
})();

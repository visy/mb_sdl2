# quad.fi first-time setup

One-time root steps on the Linode. After this, `wasm/deploy.ps1` handles every redeploy. Signal server is Go: source is uploaded, compiled in-place by the service user, run as a static binary under systemd.

## 1. Install Go

```bash
sudo apt update
sudo apt install -y golang-go
go version    # need >= 1.22 (Debian 12 ships 1.19; if too old, use tarball below)
```

If apt's go is too old:
```bash
curl -fsSLO https://go.dev/dl/go1.22.6.linux-amd64.tar.gz
sudo rm -rf /usr/local/go && sudo tar -C /usr/local -xzf go1.22.6.linux-amd64.tar.gz
echo 'export PATH=$PATH:/usr/local/go/bin' | sudo tee /etc/profile.d/go.sh
source /etc/profile.d/go.sh
```

## 2. Prep dirs / user (deploy.ps1 also does this idempotently)

```bash
sudo useradd --system --home /opt/mb-signal --shell /usr/sbin/nologin mbsignal
sudo mkdir -p /opt/mb-signal
sudo chown mbsignal:mbsignal /opt/mb-signal
```

## 3. Run `deploy.ps1` once from your Windows box

```powershell
cd D:\dev\mb_src\wasm
.\deploy.ps1
```

Uploads `server.go` + `go.mod` to `/opt/mb-signal/src/`, runs `go mod tidy && go build` as the `mbsignal` user, installs `/etc/systemd/system/mb-signal.service`, enables + starts it.

Verify:
```bash
sudo systemctl status mb-signal
ss -tlnp | grep 7000           # node-free; listens directly on :7000
sudo journalctl -u mb-signal -f
```

## 4. Wire nginx

Add inside your existing `quad.fi` `server { ... }` block:

```nginx
location = /mb/ws {
    proxy_pass http://127.0.0.1:7000;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
    proxy_read_timeout 3600s;
    proxy_send_timeout 3600s;
}
```

Add the connection-limit zone to nginx's `http {}` block (file `/etc/nginx/nginx.conf`, near the top of `http { ... }`):

```nginx
limit_conn_zone $binary_remote_addr zone=mb_ws:10m;
```

Reload:
```bash
sudo nginx -t && sudo systemctl reload nginx
```

## 5. Smoke test

```bash
curl -i https://quad.fi/mb/
# WebSocket: any WS client works; example using websocat:
websocat wss://quad.fi/mb/ws
```

Browser test: open `https://quad.fi/mb/` in two tabs, host a room in one, join in the other.

## Ongoing deploys

```powershell
.\deploy.ps1                # static + signal (rebuild + reupload + restart)
.\deploy.ps1 -NoBuild       # skip emcc rebuild of game
.\deploy.ps1 -StaticOnly    # game only
.\deploy.ps1 -SignalOnly    # signal only
```

`-SignalOnly` re-runs `go build` on the server. If `server.go` changed, binary gets rebuilt and service restarts. Module deps are cached under `/opt/mb-signal/go/`.

## TURN (optional, NAT traversal)

Many home networks need TURN.

```bash
sudo apt install -y coturn
sudo nano /etc/turnserver.conf   # set realm=quad.fi, listening-port=3478, static-auth-secret=...
sudo systemctl enable --now coturn
```

Open UDP 3478 + the relay range (default 49152-65535) in the Linode firewall. Edit `wasm/netplay.js` RTC_CONFIG to add the TURN entry with your shared secret.

## Logs

```bash
sudo journalctl -u mb-signal -f
```

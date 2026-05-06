#include "web.h"

#include <ESP8266WebServer.h>

#include "tracker.h"

static ESP8266WebServer server(80);

static const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>BTC Tracker</title>
<style>
body{font-family:system-ui,sans-serif;margin:1rem;max-width:40rem;}
input{width:100%;box-sizing:border-box;padding:.5rem;margin:.25rem 0;}
button{padding:.5rem 1rem;margin-top:.5rem;}
#status{white-space:pre-wrap;border:1px solid #ccc;padding:.75rem;margin-top:1rem;min-height:4rem;background:#fafafa;}
</style>
</head>
<body>
<h1>BTC Tracker</h1>
<p>Submit a 64-character hex Bitcoin txid. Status is polled from the device.</p>
<form id="f">
<label>txid</label>
<input id="txid" name="txid" maxlength="64" placeholder="txid de 64 hex" autocomplete="off"/>
<button type="submit">Acompanhar</button>
</form>
<div id="status">Idle.</div>
<script>
const st=document.getElementById('status');
async function poll(){
  try{
    const r=await fetch('/status');
    const j=await r.json();
    st.textContent=j.line||JSON.stringify(j);
  }catch(e){ st.textContent='poll error: '+e; }
}
setInterval(poll,2000);
poll();
document.getElementById('f').addEventListener('submit',async (e)=>{
  e.preventDefault();
  const txid=document.getElementById('txid').value.trim();
  const body='txid='+encodeURIComponent(txid);
  st.textContent='Sending...';
  try{
    const r=await fetch('/track',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const t=await r.text();
    st.textContent=t;
    poll();
  }catch(err){ st.textContent='request error: '+err; }
});
</script>
</body>
</html>
)rawliteral";

static void handle_root() { server.send_P(200, "text/html", HTML_INDEX); }

static void handle_track() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "method not allowed");
    return;
  }
  if (!server.hasArg("txid")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing txid\"}");
    return;
  }
  String txid = server.arg("txid");
  txid.trim();
  if (!tracker_start(txid.c_str())) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid txid\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_status() { server.send(200, "application/json", tracker_status_json()); }

void web_setup() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/track", HTTP_POST, handle_track);
  server.on("/status", HTTP_GET, handle_status);
  server.begin();
}

void web_loop() { server.handleClient(); }

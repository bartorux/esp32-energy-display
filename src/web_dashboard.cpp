#include "web_dashboard.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

static WebServer server(80);
static WebContext ctx;
static unsigned long bootTime;

static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP Energy</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,system-ui,sans-serif;background:#0f0f1a;color:#c8d0e0;padding:12px;max-width:520px;margin:0 auto}
h1{font-size:1.1em;text-align:center;color:#64748b;margin:8px 0 12px;letter-spacing:.5px}
h2{font-size:.9em;color:#8892b0;margin-bottom:8px;display:flex;align-items:center;gap:6px}
.c{background:#161b2e;border-radius:14px;padding:14px;margin-bottom:10px;border:1px solid #1e2740}
.big{font-size:2.8em;font-weight:700;text-align:center;color:#fff;line-height:1.1}
.unit{text-align:center;color:#64748b;font-size:.85em;margin:2px 0 6px}
.row{display:flex;justify-content:space-around;text-align:center}
.row div{flex:1}.lb{font-size:.7em;color:#64748b;text-transform:uppercase}.vl{font-size:1.2em;font-weight:600}
.g{color:#34d399}.r{color:#f87171}.y{color:#fbbf24}
.ch{display:flex;align-items:flex-end;height:56px;gap:1px;margin-top:8px;padding-top:6px;border-top:1px solid #1e2740}
.ch .b{flex:1;background:#334155;border-radius:2px 2px 0 0;min-height:1px;transition:background .2s}
.ch .b.hl{background:#38bdf8}.ch .b:hover{background:#60a5fa}
.ch .b.cheap{background:#34d399}.ch .b.exp{background:#f87171}
.hrs{display:flex;justify-content:space-between;font-size:.6em;color:#475569;margin-top:2px;padding:0 1px}
table{width:100%;border-collapse:collapse;font-size:.85em}
th{text-align:left;color:#64748b;font-weight:500;padding:4px 6px;border-bottom:1px solid #1e2740}
td{padding:4px 6px;border-bottom:1px solid #111827}
.sl{width:100%;accent-color:#38bdf8;margin:6px 0}
.btn{background:#1e3a5f;color:#93c5fd;border:1px solid #2563eb;border-radius:8px;padding:8px 16px;font-size:.9em;cursor:pointer;width:100%;margin-top:6px}
.btn:hover{background:#1e40af}.btn:active{transform:scale(.98)}
.btn.on{background:#065f46;border-color:#10b981;color:#6ee7b7}
.inf{display:grid;grid-template-columns:1fr 1fr;gap:4px 12px;font-size:.8em}
.inf .k{color:#64748b}.inf .v{color:#c8d0e0;text-align:right}
.cmp{text-align:center;font-size:.85em;margin-top:4px}
.tag{display:inline-block;padding:1px 6px;border-radius:4px;font-size:.8em;font-weight:600}
#st{text-align:center;font-size:.75em;color:#475569;margin-top:8px}
</style></head><body>
<h1>ESP ENERGY MONITOR</h1>
<div id="app">Ladowanie...</div>
<div id="st"></div>
<script>
const $ = id => document.getElementById(id);
let D={},H=[];

function bar(arr,mn,mx,hlH,cheapH,expH){
 let rng=mx-mn||1,s='<div class="ch">';
 for(let i=0;i<24;i++){
  let v=arr[i]||0,p=Math.max(3,((v-mn)/rng)*100);
  let cl='b';if(i===hlH)cl+=' hl';if(i===cheapH)cl+=' cheap';if(i===expH)cl+=' exp';
  s+=`<div class="${cl}" style="height:${p}%" title="${i}:00 ${v.toFixed(0)} PLN"></div>`;
 }
 return s+'</div><div class="hrs"><span>0</span><span>6</span><span>12</span><span>18</span><span>23</span></div>';
}

function uptime(s){
 if(s<60)return s+'s';
 if(s<3600)return Math.floor(s/60)+'m';
 let h=Math.floor(s/3600),m=Math.floor((s%3600)/60);
 return h+'h '+m+'m';
}

function render(){
 let o='';
 if(D.today){
  let t=D.today;
  o+=`<div class="c"><h2>TERAZ</h2>
  <div class="big">${t.current.toFixed(0)}</div>
  <div class="unit">PLN/MWh</div>`;
  if(D.yAvg>0){
   let df=((t.avg-D.yAvg)/D.yAvg*100);
   let cls=df>0?'r':'g',arr=df>0?'&#9650;':'&#9660;';
   o+=`<div class="cmp"><span class="tag ${cls}">${arr} ${Math.abs(df).toFixed(1)}% vs wczoraj</span></div>`;
  }
  o+=`<div class="row" style="margin-top:8px">
   <div><div class="lb">Min</div><div class="vl g">${t.min.toFixed(0)}</div></div>
   <div><div class="lb">Avg</div><div class="vl">${t.avg.toFixed(0)}</div></div>
   <div><div class="lb">Max</div><div class="vl r">${t.max.toFixed(0)}</div></div>
   <div><div class="lb">Tanio</div><div class="vl g">${t.cheapestH}:00</div></div>
  </div>`;
  o+=bar(t.hourly,t.min,t.max,t.hour,t.cheapestH,t.expH);
  o+='</div>';
 }
 if(D.tomorrow){
  let m=D.tomorrow;
  o+=`<div class="c"><h2>JUTRO</h2>
  <div class="row">
   <div><div class="lb">Min</div><div class="vl g">${m.min.toFixed(0)}</div></div>
   <div><div class="lb">Avg</div><div class="vl">${m.avg.toFixed(0)}</div></div>
   <div><div class="lb">Max</div><div class="vl r">${m.max.toFixed(0)}</div></div>
   <div><div class="lb">Tanio</div><div class="vl g">${m.cheapestH}:00</div></div>
  </div>`;
  if(m.hourly)o+=bar(m.hourly,m.min,m.max,-1,m.cheapestH,m.expH);
  o+='</div>';
 }
 if(H.length>1){
  let avgs=H.map(e=>e.av),mn=Math.min(...avgs),mx=Math.max(...avgs),rng=mx-mn||1;
  let wavg=(avgs.reduce((a,b)=>a+b,0)/avgs.length).toFixed(0);
  o+=`<div class="c"><h2>TREND (${H.length}d) avg ${wavg}</h2>`;
  o+='<div class="ch" style="height:70px">';
  for(let i=0;i<H.length;i++){
   let p=Math.max(5,((avgs[i]-mn)/rng)*100);
   let cl='b';if(avgs[i]===mn)cl+=' cheap';if(avgs[i]===mx)cl+=' exp';if(i===H.length-1)cl+=' hl';
   o+=`<div class="${cl}" style="height:${p}%;min-width:12px" title="${H[i].d} avg ${avgs[i]}"></div>`;
  }
  o+='</div><div class="hrs">';
  o+=`<span>${H[0].d.slice(5)}</span>`;
  if(H.length>2)o+=`<span>${H[Math.floor(H.length/2)].d.slice(5)}</span>`;
  o+=`<span>${H[H.length-1].d.slice(5)}</span>`;
  o+='</div></div>';
 }
 if(H.length){
  o+=`<div class="c"><h2>HISTORIA</h2><table>
  <tr><th>Data</th><th>Avg</th><th>Min</th><th>Max</th><th>Tanio</th></tr>`;
  for(let i=H.length-1;i>=0;i--){
   let e=H[i];
   o+=`<tr><td>${e.d.slice(5)}</td><td>${e.av}</td><td class="g">${e.mn}</td><td class="r">${e.mx}</td><td>${e.ch}:00</td></tr>`;
  }
  o+='</table></div>';
 }
 if(D.sys){
  let s=D.sys;
  o+=`<div class="c"><h2>USTAWIENIA</h2>
  <button class="btn${s.autoBri?' on':''}" id="ab">Jasnosc: ${s.autoBri?'AUTO':'MANUAL'} (${s.brightness}%)</button>
  <div id="bwrap" style="display:${s.autoBri?'none':'block'}">
  <label style="font-size:.85em">Jasnosc: <b id="bv">${s.brightness}%</b></label>
  <input type="range" class="sl" min="5" max="100" value="${s.brightness}" oninput="$('bv').textContent=this.value+'%'" onchange="setBri(this.value)">
  </div>
  <button class="btn${s.ssEnabled?' on':''}" id="ss">Wygaszacz: ${s.ssEnabled?'ON':'OFF'}</button>
  <button class="btn" id="rb">Odswiez dane z PSE</button>
  </div>`;
  o+=`<div class="c"><h2>SYSTEM</h2><div class="inf">
  <span class="k">Heap</span><span class="v">${(s.heap/1024).toFixed(1)} KB (min ${(s.heapMin/1024).toFixed(1)})</span>
  <span class="k">RSSI</span><span class="v">${s.rssi} dBm</span>
  <span class="k">IP</span><span class="v">${s.ip}</span>
  <span class="k">Uptime</span><span class="v">${uptime(s.uptime)}</span>
  <span class="k">Wygaszacz</span><span class="v">${s.ssEnabled?'ON':'OFF'}</span>
  </div></div>`;
 }
 $('app').innerHTML=o;
 if(D.time)$('st').textContent='Czas ESP: '+D.time+' | heap '+((D.sys&&D.sys.heap)||'?')+' | auto 60s';
 // Wire up buttons
 let rb=$('rb');
 if(rb)rb.onclick=()=>{rb.textContent='Odswiezam...';
  fetch('/api/refresh',{method:'POST'}).then(()=>{setTimeout(()=>location.reload(),8000)});};
 let ss=$('ss');
 if(ss)ss.onclick=()=>{ss.textContent='...';
  fetch('/api/screensaver',{method:'POST'}).then(()=>load());};
 let ab=$('ab');
 if(ab)ab.onclick=()=>{ab.textContent='...';
  fetch('/api/autobri',{method:'POST'}).then(()=>load());};
}

function setBri(v){fetch('/api/brightness?val='+v,{method:'POST'});}

async function load(){
 try{
  let [api,hist]=await Promise.all([fetch('/api').then(r=>r.json()),fetch('/api/history').then(r=>r.json())]);
  D=api;H=hist;render();
 }catch(e){$('app').innerHTML='<div class="c" style="text-align:center">Blad polaczenia<br>'+e+'</div>';}
}
load();
setInterval(load,60000);
</script></body></html>
)rawliteral";

static void handleRoot() {
    server.send(200, "text/html", PAGE_HTML);
}

static void handleApi() {
    JsonDocument doc;

    struct tm ti;
    if (getLocalTime(&ti)) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
        doc["time"] = buf;
    }

    if (*ctx.todayOk) {
        JsonObject today = doc["today"].to<JsonObject>();
        today["current"] = ctx.today->currentPrice;
        today["min"] = ctx.today->minPrice;
        today["max"] = ctx.today->maxPrice;
        today["avg"] = ctx.today->avgPrice;
        today["cheapestH"] = ctx.today->cheapestHour;
        today["expH"] = ctx.today->expensiveHour;
        today["hour"] = 0;
        if (getLocalTime(&ti)) today["hour"] = ti.tm_hour;
        JsonArray hourly = today["hourly"].to<JsonArray>();
        for (int i = 0; i < 24; i++) hourly.add(ctx.today->hourlyAvg[i]);
    }

    if (*ctx.tomorrowOk) {
        JsonObject tmr = doc["tomorrow"].to<JsonObject>();
        tmr["min"] = ctx.tomorrow->minPrice;
        tmr["max"] = ctx.tomorrow->maxPrice;
        tmr["avg"] = ctx.tomorrow->avgPrice;
        tmr["cheapestH"] = ctx.tomorrow->cheapestHour;
        tmr["expH"] = ctx.tomorrow->expensiveHour;
        JsonArray hourly = tmr["hourly"].to<JsonArray>();
        for (int i = 0; i < 24; i++) hourly.add(ctx.tomorrow->hourlyAvg[i]);
    }

    // Yesterday avg for comparison
    DayRecord yrec;
    if (storageGetYesterday(yrec)) {
        doc["yAvg"] = yrec.avgPrice;
    }

    // System info
    JsonObject sys = doc["sys"].to<JsonObject>();
    sys["heap"] = ESP.getFreeHeap();
    sys["heapMin"] = ESP.getMinFreeHeap();
    sys["rssi"] = WiFi.RSSI();
    sys["ip"] = WiFi.localIP().toString();
    sys["uptime"] = (millis() - bootTime) / 1000;
    sys["brightness"] = *ctx.brightness;
    sys["screensaver"] = *ctx.screenSaverOn;
    sys["ssEnabled"] = *ctx.screensaverEnabled;
    sys["autoBri"] = !(*ctx.manualBrightness);

    // Stream JSON directly to client (avoid String allocation)
    size_t len = measureJson(doc);
    server.setContentLength(len);
    server.send(200, "application/json", "");
    WiFiClient client = server.client();
    serializeJson(doc, client);
}

static void handleHistory() {
    // Stream directly from LittleFS to avoid heap fragmentation
    File f = LittleFS.open("/history.json", "r");
    if (!f || f.size() == 0) {
        server.send(200, "application/json", "[]");
        if (f) f.close();
        return;
    }
    server.streamFile(f, "application/json");
    f.close();
}

static void handleRefresh() {
    server.send(200, "application/json", "{\"ok\":true}");
    if (ctx.onRefresh) ctx.onRefresh();
}

static void handleScreensaver() {
    if (ctx.onScreensaverToggle) ctx.onScreensaverToggle();
    String state = *ctx.screensaverEnabled ? "true" : "false";
    server.send(200, "application/json", "{\"ok\":true,\"enabled\":" + state + "}");
}

static void handleAutoBri() {
    if (ctx.onAutoBriToggle) ctx.onAutoBriToggle();
    bool isAuto = !(*ctx.manualBrightness);
    String json = "{\"ok\":true,\"auto\":" + String(isAuto ? "true" : "false") + "}";
    server.send(200, "application/json", json);
}

static void handleBrightness() {
    if (server.hasArg("val")) {
        int v = constrain(server.arg("val").toInt(), 0, 100);
        if (ctx.onBrightness) ctx.onBrightness((uint8_t)v);
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(400, "application/json", "{\"error\":\"missing val\"}");
    }
}

void webserverInit(const WebContext &c) {
    ctx = c;
    bootTime = millis();

    server.on("/", handleRoot);
    server.on("/api", handleApi);
    server.on("/api/history", handleHistory);
    server.on("/api/refresh", HTTP_POST, handleRefresh);
    server.on("/api/brightness", HTTP_POST, handleBrightness);
    server.on("/api/autobri", HTTP_POST, handleAutoBri);
    server.on("/api/screensaver", HTTP_POST, handleScreensaver);
    server.begin();
    Serial.println("[WEB] Server started on port 80");
}

void webserverHandle() {
    server.handleClient();
}

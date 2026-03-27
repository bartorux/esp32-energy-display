#include "web_dashboard.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ─── Instancje serwerów ───────────────────────────────────────────────────────

static WebServer  server(80);
static WebServer  apServer(80);
static DNSServer  dnsServer;      // captive portal DNS (port 53)
static WebContext ctx;
static unsigned long bootTime;

// ─────────────────────────────────────────────────────────────────────────────
//  TRYB STA – pełny dashboard z cenami energii
// ─────────────────────────────────────────────────────────────────────────────

static const char STA_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP Energy</title>
<style>
:root{--bg:#f0f2f5;--card:#fff;--text:#1a1a2e;--text2:#1f2937;--muted:#6b7280;--muted2:#9ca3af;--border:#e2e5ea;--border2:#e5e7eb;--border3:#f3f4f6;--bar:#cbd5e1;--accent:#1d4ed8;--accent2:#2563eb;--accent3:#3b82f6;--shadow:rgba(0,0,0,.06)}
body.dark{--bg:#0f1117;--card:#1a1d27;--text:#e2e5ea;--text2:#d1d5db;--muted:#9ca3af;--muted2:#6b7280;--border:#2d3140;--border2:#374151;--border3:#1f2937;--bar:#4b5563;--accent:#60a5fa;--accent2:#3b82f6;--accent3:#2563eb;--shadow:rgba(0,0,0,.3)}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg);color:var(--text);padding:12px;max-width:520px;margin:0 auto;transition:background .3s,color .3s}
h1{font-size:1.1em;text-align:center;color:var(--accent);margin:8px 0 12px;letter-spacing:.5px}
h2{font-size:.9em;color:var(--muted);margin-bottom:8px;display:flex;align-items:center;gap:6px}
.c{background:var(--card);border-radius:14px;padding:14px;margin-bottom:10px;border:1px solid var(--border);box-shadow:0 1px 3px var(--shadow);transition:background .3s,border-color .3s}
.big{font-size:2.8em;font-weight:700;text-align:center;color:var(--text2);line-height:1.1}
.unit{text-align:center;color:var(--muted);font-size:.85em;margin:2px 0 6px}
.row{display:flex;justify-content:space-around;text-align:center}
.row div{flex:1}.lb{font-size:.7em;color:var(--muted);text-transform:uppercase}.vl{font-size:1.2em;font-weight:600;color:var(--text2)}
.g{color:#059669}.r{color:#dc2626}.y{color:#d97706}
.ch{display:flex;align-items:flex-end;height:56px;gap:1px;margin-top:8px;padding-top:6px;border-top:1px solid var(--border2)}
.ch .b{flex:1;background:var(--bar);border-radius:2px 2px 0 0;min-height:1px;transition:background .2s}
.ch .b.hl{background:var(--accent3)}.ch .b:hover{background:#60a5fa}
.ch .b.cheap{background:#10b981}.ch .b.exp{background:#ef4444}
.hrs{display:flex;justify-content:space-between;font-size:.6em;color:var(--muted2);margin-top:2px;padding:0 1px}
table{width:100%;border-collapse:collapse;font-size:.85em}
th{text-align:left;color:var(--muted);font-weight:500;padding:4px 6px;border-bottom:1px solid var(--border2)}
td{padding:4px 6px;border-bottom:1px solid var(--border3)}
.sl{width:100%;accent-color:var(--accent3);margin:6px 0}
.btn{background:var(--accent2);color:#fff;border:1px solid var(--accent);border-radius:8px;padding:8px 16px;font-size:.9em;cursor:pointer;width:100%;margin-top:6px}
.btn:hover{background:var(--accent)}.btn:active{transform:scale(.98)}
.btn.on{background:#059669;border-color:#047857;color:#fff}
.btn.danger{background:#dc2626;border-color:#b91c1c;color:#fff}
.inf{display:grid;grid-template-columns:1fr 1fr;gap:4px 12px;font-size:.8em}
.inf .k{color:var(--muted)}.inf .v{color:var(--text2);text-align:right}
.cmp{text-align:center;font-size:.85em;margin-top:4px}
.tag{display:inline-block;padding:1px 6px;border-radius:4px;font-size:.8em;font-weight:600}
#st{text-align:center;font-size:.75em;color:var(--muted2);margin-top:8px}
#thm{position:fixed;top:8px;right:8px;background:var(--card);border:1px solid var(--border);border-radius:50%;width:36px;height:36px;cursor:pointer;font-size:1.2em;display:flex;align-items:center;justify-content:center;z-index:10;box-shadow:0 1px 4px var(--shadow)}
</style></head><body>
<button id="thm" onclick="toggleTheme()"></button>
<script>
function toggleTheme(){document.body.classList.toggle('dark');localStorage.setItem('theme',document.body.classList.contains('dark')?'dark':'light');document.getElementById('thm').textContent=document.body.classList.contains('dark')?'\u2600':'\u263E';}
if(localStorage.getItem('theme')==='dark'){document.body.classList.add('dark');document.getElementById('thm').textContent='\u2600';}else{document.getElementById('thm').textContent='\u263E';}
</script>
<h1>&#x26A1; ESP ENERGY MONITOR</h1>
<div id="app">&#x0141;adowanie...</div>
<div id="st"></div>
<script>
const $=id=>document.getElementById(id);
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
  // Wygaszacz: btn.on gdy ENABLED, domyslnie off
  let ssLabel=s.ssEnabled?'Wygaszacz: W\u0141 (po 2min)':'Wygaszacz: WY\u0141';
  o+=`<div class="c"><h2>USTAWIENIA</h2>
  <button class="btn${s.autoBri?' on':''}" id="ab">Jasno\u015b\u0107: ${s.autoBri?'AUTO':'R\u0118CZNA'} (${s.brightness}%)</button>
  <div id="bwrap" style="display:${s.autoBri?'none':'block'}">
  <label style="font-size:.85em">Jasno\u015b\u0107: <b id="bv">${s.brightness}%</b></label>
  <input type="range" class="sl" min="5" max="100" value="${s.brightness}" oninput="$('bv').textContent=this.value+'%'" onchange="setBri(this.value)">
  </div>
  <button class="btn${s.ssEnabled?' on':''}" id="ss">${ssLabel}</button>
  <label style="font-size:.85em;margin-top:10px">Alert cenowy: <b id="atv">${s.alertThreshold>0?s.alertThreshold+' PLN/MWh':'WY\u0141'}</b></label>
  <input type="range" class="sl" min="0" max="800" step="10" value="${s.alertThreshold||0}" id="atsl" oninput="$('atv').textContent=this.value>0?this.value+' PLN/MWh':'WY\u0141'" onchange="setAlert(this.value)">
  <button class="btn" id="rb">&#x27F3; Od\u015bwie\u017c dane z PSE</button>
  <button class="btn danger" id="wf" style="margin-top:12px">&#x26A0; Zmie\u0144 WiFi (restart w trybie AP)</button>
  </div>`;
  o+=`<div class="c"><h2>SYSTEM</h2><div class="inf">
  <span class="k">Heap</span><span class="v">${(s.heap/1024).toFixed(1)} KB (min ${(s.heapMin/1024).toFixed(1)})</span>
  <span class="k">RSSI</span><span class="v">${s.rssi} dBm</span>
  <span class="k">IP</span><span class="v">${s.ip}</span>
  <span class="k">Uptime</span><span class="v">${uptime(s.uptime)}</span>
  <span class="k">SSID</span><span class="v" id="ssidv"></span>
  </div></div>`;
 }
 $('app').innerHTML=o;
 let sv=$('ssidv');if(sv)sv.textContent=D.sys&&D.sys.ssid?D.sys.ssid:'?';
 if(D.time)$('st').textContent='Czas ESP: '+D.time+' | auto 60s';
 // Wire up buttons
 let rb=$('rb');
 if(rb)rb.onclick=()=>{rb.textContent='Od\u015bwie\u017cam...';
  fetch('/api/refresh',{method:'POST'}).then(()=>setTimeout(()=>location.reload(),8000));};
 let ss=$('ss');
 if(ss)ss.onclick=()=>{ss.textContent='...';
  fetch('/api/screensaver',{method:'POST'}).then(()=>load());};
 let ab=$('ab');
 if(ab)ab.onclick=()=>{ab.textContent='...';
  fetch('/api/autobri',{method:'POST'}).then(()=>load());};
 let wf=$('wf');
 if(wf)wf.onclick=()=>{
  if(confirm('Urz\u0105dzenie uruchomi si\u0119 w trybie konfiguracji WiFi.\nPo\u0142\u0105cz si\u0119 z sieci\u0105 "ESP-Energy-Setup" i otw\u00f3rz 192.168.4.1\n\nKontynuowa\u0107?')){
   wf.textContent='Restartuj\u0119...';
   fetch('/api/wifi-reset',{method:'POST'});
  }};
}

function setBri(v){fetch('/api/brightness?val='+v,{method:'POST'});}
function setAlert(v){fetch('/api/alert-threshold?val='+v,{method:'POST'});}

async function load(){
 try{
  let [api,hist]=await Promise.all([
   fetch('/api').then(r=>r.json()),
   fetch('/api/history').then(r=>r.json())
  ]);
  D=api;H=hist;render();
 }catch(e){
  $('app').innerHTML='<div class="c" style="text-align:center">Blad polaczenia<br>'+e+'</div>';
 }
}
load();
setInterval(load,60000);
</script></body></html>
)rawliteral";

// ─────────────────────────────────────────────────────────────────────────────
//  TRYB AP – captive portal: konfiguracja WiFi + informacje
// ─────────────────────────────────────────────────────────────────────────────

static const char AP_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP Energy – Konfiguracja WiFi</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,system-ui,sans-serif;background:#f0f2f5;color:#1a1a2e;padding:16px;max-width:460px;margin:0 auto}
h1{font-size:1.15em;text-align:center;color:#1d4ed8;margin:12px 0 4px}
.sub{text-align:center;font-size:.8em;color:#6b7280;margin-bottom:16px}
.c{background:#fff;border-radius:14px;padding:16px;margin-bottom:12px;border:1px solid #e2e5ea;box-shadow:0 1px 3px rgba(0,0,0,.06)}
h2{font-size:.9em;color:#4b5563;margin-bottom:10px}
label{display:block;font-size:.82em;color:#6b7280;margin-bottom:3px;margin-top:10px}
input[type=text],input[type=password]{width:100%;background:#f9fafb;border:1px solid #d1d5db;border-radius:8px;padding:9px 12px;color:#111827;font-size:.95em}
input:focus{outline:none;border-color:#3b82f6;box-shadow:0 0 0 3px rgba(59,130,246,.15)}
.btn{display:block;width:100%;background:#2563eb;color:#fff;border:none;border-radius:8px;padding:11px;font-size:.95em;cursor:pointer;margin-top:14px;font-weight:600}
.btn:hover{background:#1d4ed8}.btn:active{transform:scale(.98)}
.btn.sec{background:#059669;color:#fff}
#msg{text-align:center;padding:10px;border-radius:8px;margin-top:10px;font-size:.88em;display:none}
.ok{background:#ecfdf5;color:#065f46;border:1px solid #6ee7b7}
.err{background:#fef2f2;color:#991b1b;border:1px solid #fca5a5}
.inf{display:grid;grid-template-columns:1fr 1fr;gap:4px 12px;font-size:.82em}
.inf .k{color:#6b7280}.inf .v{color:#1f2937;text-align:right}
.warn{background:#fffbeb;border:1px solid #f59e0b;border-radius:8px;padding:10px;font-size:.82em;color:#92400e;margin-top:8px}
.scan-list{margin-top:8px}
.net{display:flex;align-items:center;justify-content:space-between;padding:9px 4px;border-bottom:1px solid #e5e7eb;cursor:pointer}
.net:hover{background:#f0f7ff}.ns{font-size:.9em;color:#111827}.rssi{font-size:.75em;color:#9ca3af}
</style></head><body>
<h1>&#x26A1; ESP Energy Setup</h1>
<div class="sub">Tryb konfiguracji WiFi &nbsp;&#x2022;&nbsp; 192.168.4.1</div>

<div class="c">
<h2>Konfiguracja WiFi</h2>
<label>Nazwa sieci (SSID)</label>
<input type="text" id="ssid" placeholder="Wpisz lub wybierz z listy poni&#x017C;ej">
<label>Has&#x0142;o</label>
<input type="password" id="pass" placeholder="Has&#x0142;o WiFi (puste = sie&#x0107; otwarta)">
<button class="btn" onclick="save()">&#x2714; Zapisz i po&#x0142;&#x0105;cz</button>
<div id="msg"></div>
<div class="warn">&#x26A0; Po zapisaniu urz&#x0105;dzenie uruchomi si&#x0119; ponownie i po&#x0142;&#x0105;czy z WiFi.</div>
</div>

<div class="c">
<h2>Dost&#x0119;pne sieci</h2>
<button class="btn sec" style="margin-top:0" onclick="scan()">&#x1F50D; Skanuj sieci WiFi</button>
<div class="scan-list" id="nets"></div>
</div>

<div class="c">
<h2>System</h2>
<div class="inf" id="sysinfo">&#x0141;adowanie...</div>
</div>

<script>
function msg(txt,ok){
 let m=document.getElementById('msg');
 m.className=ok?'ok':'err';m.textContent=txt;m.style.display='block';
}

function save(){
 let s=document.getElementById('ssid').value.trim();
 let p=document.getElementById('pass').value;
 if(!s){msg('Podaj nazw\u0119 sieci!',false);return;}
 if(!p&&!confirm('Has\u0142o jest puste. Czy to sie\u0107 otwarta (bez has\u0142a)?'))return;
 msg('Zapisuj\u0119 i restartuj\u0119...', true);
 fetch('/api/wifi-save',{
  method:'POST',
  headers:{'Content-Type':'application/json'},
  body:JSON.stringify({ssid:s,pass:p})
 }).then(r=>r.json()).then(d=>{
  if(d.ok){msg('Zapisano! Restart za 3s\u2026', true);}
  else{msg('B\u0142\u0105d: '+d.error, false);}
 }).catch(()=>msg('Wys\u0142ano (restart\u2026)', true));
}

function scan(){
 let n=document.getElementById('nets');
 n.innerHTML='<div style="color:#6b7280;padding:8px 0">Skanowanie\u2026</div>';
 fetch('/api/wifi-scan').then(r=>r.json()).then(nets=>{
  if(!nets.length){n.innerHTML='<div style="color:#475569;padding:8px 0">Brak sieci</div>';return;}
  n.innerHTML='';
  nets.forEach(function(net){
   let d=document.createElement('div');d.className='net';
   let ns=document.createElement('span');ns.className='ns';ns.textContent=net.ssid;
   let rs=document.createElement('span');rs.className='rssi';
   rs.textContent=net.rssi+' dBm '+(net.enc?'\u{1F512}':'');
   d.appendChild(ns);d.appendChild(rs);
   d.addEventListener('click',function(){
    document.getElementById('ssid').value=net.ssid;
    document.getElementById('pass').value='';
    document.getElementById('pass').focus();
   });
   n.appendChild(d);
  });
 }).catch(()=>{n.innerHTML='<div style="color:#f87171;padding:8px 0">Blad skanowania</div>';});
}

fetch('/api/sys').then(r=>r.json()).then(s=>{
 document.getElementById('sysinfo').innerHTML=`
  <span class="k">Heap</span><span class="v">${(s.heap/1024).toFixed(1)} KB</span>
  <span class="k">Chip</span><span class="v">ESP32-C3</span>
  <span class="k">MAC</span><span class="v">${s.mac}</span>
  <span class="k">Hostname</span><span class="v">esp-energy</span>`;
}).catch(()=>{document.getElementById('sysinfo').innerHTML='?';});
</script>
</body></html>
)rawliteral";

// ─── STA handlers ─────────────────────────────────────────────────────────────

static void staHandleRoot() {
    server.send_P(200, "text/html", STA_PAGE);
}

static void staHandleApi() {
    JsonDocument doc;
    struct tm ti;
    if (getLocalTime(&ti)) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
        doc["time"] = buf;
    }
    if (*ctx.todayOk) {
        JsonObject today = doc["today"].to<JsonObject>();
        today["current"]  = ctx.today->currentPrice;
        today["min"]      = ctx.today->minPrice;
        today["max"]      = ctx.today->maxPrice;
        today["avg"]      = ctx.today->avgPrice;
        today["cheapestH"] = ctx.today->cheapestHour;
        today["expH"]     = ctx.today->expensiveHour;
        today["hour"]     = 0;
        if (getLocalTime(&ti)) today["hour"] = ti.tm_hour;
        JsonArray hourly = today["hourly"].to<JsonArray>();
        for (int i = 0; i < 24; i++) hourly.add(ctx.today->hourlyAvg[i]);
    }
    if (*ctx.tomorrowOk) {
        JsonObject tmr = doc["tomorrow"].to<JsonObject>();
        tmr["min"]      = ctx.tomorrow->minPrice;
        tmr["max"]      = ctx.tomorrow->maxPrice;
        tmr["avg"]      = ctx.tomorrow->avgPrice;
        tmr["cheapestH"] = ctx.tomorrow->cheapestHour;
        tmr["expH"]     = ctx.tomorrow->expensiveHour;
        JsonArray hourly = tmr["hourly"].to<JsonArray>();
        for (int i = 0; i < 24; i++) hourly.add(ctx.tomorrow->hourlyAvg[i]);
    }
    DayRecord yrec;
    if (storageGetYesterday(yrec)) doc["yAvg"] = yrec.avgPrice;

    JsonObject sys = doc["sys"].to<JsonObject>();
    sys["heap"]       = ESP.getFreeHeap();
    sys["heapMin"]    = ESP.getMinFreeHeap();
    sys["rssi"]       = WiFi.RSSI();
    sys["ip"]         = WiFi.localIP().toString();
    sys["ssid"]       = WiFi.SSID();
    sys["uptime"]     = (millis() - bootTime) / 1000;
    sys["brightness"] = *ctx.brightness;
    sys["screensaver"] = *ctx.screenSaverOn;
    sys["ssEnabled"]  = *ctx.screensaverEnabled;
    sys["autoBri"]    = !(*ctx.manualBrightness);
    if (ctx.priceAlertThreshold)
        sys["alertThreshold"] = *ctx.priceAlertThreshold;

    size_t len = measureJson(doc);
    server.setContentLength(len);
    server.send(200, "application/json", "");
    WiFiClient client = server.client();
    serializeJson(doc, client);
}

static void staHandleHistory() {
    File f = LittleFS.open(HISTORY_PATH, "r");
    if (!f || f.size() == 0) {
        server.send(200, "application/json", "[]");
        if (f) f.close();
        return;
    }
    server.streamFile(f, "application/json");
    f.close();
}

static void staHandleRefresh() {
    server.send(200, "application/json", "{\"ok\":true}");
    if (ctx.onRefresh) ctx.onRefresh();
}

static void staHandleScreensaver() {
    if (ctx.onScreensaverToggle) ctx.onScreensaverToggle();
    bool enabled = *ctx.screensaverEnabled;
    char json[48];
    snprintf(json, sizeof(json), "{\"ok\":true,\"enabled\":%s}", enabled ? "true" : "false");
    server.send(200, "application/json", json);
}

static void staHandleAutoBri() {
    if (ctx.onAutoBriToggle) ctx.onAutoBriToggle();
    bool isAuto = !(*ctx.manualBrightness);
    char json[40];
    snprintf(json, sizeof(json), "{\"ok\":true,\"auto\":%s}", isAuto ? "true" : "false");
    server.send(200, "application/json", json);
}

static void staHandleBrightness() {
    if (server.hasArg("val")) {
        int v = constrain(server.arg("val").toInt(), 0, 100);
        if (ctx.onBrightness) ctx.onBrightness((uint8_t)v);
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(400, "application/json", "{\"error\":\"missing val\"}");
    }
}

static void staHandleAlertThreshold() {
    if (server.hasArg("val")) {
        float v = constrain(server.arg("val").toFloat(), 0.0f, 2000.0f);
        if (ctx.onAlertThreshold) ctx.onAlertThreshold(v);
        char json[48];
        snprintf(json, sizeof(json), "{\"ok\":true,\"threshold\":%.0f}", v);
        server.send(200, "application/json", json);
    } else {
        server.send(400, "application/json", "{\"error\":\"missing val\"}");
    }
}

static void staHandleWiFiReset() {
    server.send(200, "application/json", "{\"ok\":true}");
    // Usuń zapisane kredencjały i zrestartuj → pójdzie w tryb AP
    storageDeleteWiFiCreds();
    delay(500);
    ESP.restart();
}

// ─── AP handlers ──────────────────────────────────────────────────────────────

static void apHandleRoot() {
    apServer.send_P(200, "text/html", AP_PAGE);
}

static void apHandleCaptive() {
    // Captive portal redirect – odpowiedź na wszystkie nieznane hosty
    apServer.sendHeader("Location", "http://192.168.4.1/", true);
    apServer.send(302, "text/plain", "");
}

static void apHandleSys() {
    JsonDocument doc;
    doc["heap"] = ESP.getFreeHeap();
    doc["mac"]  = WiFi.macAddress();
    size_t len = measureJson(doc);
    apServer.setContentLength(len);
    apServer.send(200, "application/json", "");
    WiFiClient cl = apServer.client();
    serializeJson(doc, cl);
}

static void apHandleWiFiSave() {
    if (!apServer.hasArg("plain")) {
        apServer.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, apServer.arg("plain"));
    if (err) {
        apServer.send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }
    const char *ssid = doc["ssid"] | "";
    const char *pass = doc["pass"] | "";
    if (strlen(ssid) == 0) {
        apServer.send(400, "application/json", "{\"error\":\"empty ssid\"}");
        return;
    }
    if (storageSaveWiFiCreds(ssid, pass)) {
        apServer.send(200, "application/json", "{\"ok\":true}");
        Serial.printf("[AP] WiFi saved: %s – restarting\n", ssid);
        // Callback do main (opcjonalnie)
        if (ctx.onWiFiSave) ctx.onWiFiSave(ssid, pass);
        delay(1500);
        ESP.restart();
    } else {
        apServer.send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

static void apHandleWiFiScan() {
    // async=false, show_hidden=false, passive=false, max_ms=500, channel=0
    int n = WiFi.scanNetworks(false, false, false, 500);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    // Deduplicate by SSID, keep strongest signal
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;  // skip hidden
        bool dup = false;
        for (JsonObject existing : arr) {
            if (ssid == existing["ssid"].as<const char*>()) {
                if (WiFi.RSSI(i) > existing["rssi"].as<int>()) {
                    existing["rssi"] = WiFi.RSSI(i);
                    existing["enc"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                }
                dup = true; break;
            }
        }
        if (!dup) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = ssid;
            o["rssi"] = WiFi.RSSI(i);
            o["enc"]  = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        }
    }
    WiFi.scanDelete();
    size_t len = measureJson(doc);
    apServer.setContentLength(len);
    apServer.send(200, "application/json", "");
    WiFiClient cl = apServer.client();
    serializeJson(doc, cl);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void webserverInit(const WebContext &c) {
    ctx = c;
    bootTime = millis();

    server.on("/",                 staHandleRoot);
    server.on("/api",              staHandleApi);
    server.on("/api/history",      staHandleHistory);
    server.on("/api/refresh",      HTTP_POST, staHandleRefresh);
    server.on("/api/brightness",   HTTP_POST, staHandleBrightness);
    server.on("/api/autobri",      HTTP_POST, staHandleAutoBri);
    server.on("/api/screensaver",  HTTP_POST, staHandleScreensaver);
    server.on("/api/alert-threshold", HTTP_POST, staHandleAlertThreshold);
    server.on("/api/wifi-reset",   HTTP_POST, staHandleWiFiReset);
    server.begin();
    Serial.printf("[WEB] STA server started — http://%s.local\n", OTA_HOSTNAME);
}

void webserverHandle() {
    server.handleClient();
}

void apServerInit(const WebContext &c) {
    ctx = c;

    // DNS catchall → 192.168.4.1 (captive portal)
    dnsServer.start(53, "*", WiFi.softAPIP());

    apServer.on("/",              apHandleRoot);
    apServer.on("/index.html",    apHandleRoot);
    apServer.on("/hotspot-detect.html", apHandleRoot);  // iOS captive
    apServer.on("/generate_204",  apHandleRoot);         // Android captive
    apServer.on("/fwlink",        apHandleRoot);         // Windows captive
    apServer.on("/api/sys",       HTTP_GET,  apHandleSys);
    apServer.on("/api/wifi-save", HTTP_POST, apHandleWiFiSave);
    apServer.on("/api/wifi-scan", HTTP_GET,  apHandleWiFiScan);
    apServer.onNotFound(apHandleCaptive);
    apServer.begin();
    Serial.printf("[AP] Captive portal started — http://%s\n", AP_IP);
}

void apServerHandle() {
    dnsServer.processNextRequest();
    apServer.handleClient();
}

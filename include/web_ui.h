#pragma once
#include <Arduino.h>

// Mobile-first cockpit portal inspired by the original SkyOrb presentation.
// All interactive endpoints are served by the ESP32 itself. No cloud UI host.
static const char PAGE_HTML[] PROGMEM = R"SKYPORTAL(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>SkyOrb | Flight Radar</title>
<style>
:root{--accent:#ff9200;--bg:#070707;--panel:#1a1a1a;--edge:#3e3e3e;--dim:#969696;--fg:#d3d3d3}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 'Courier New',monospace}button,input,select{font:inherit}
a{color:var(--accent)}a:hover{text-decoration:underline}button{cursor:pointer;touch-action:manipulation}button:disabled{opacity:.6;cursor:wait}
main{max-width:960px;padding:12px 16px 60px;margin:0 auto}header{text-align:center;padding:22px 5px 20px}header h1{margin:0;color:var(--accent);font-size:clamp(27px,5.3vw,45px);font-weight:400;letter-spacing:.17em}header .sub{letter-spacing:.27em;color:#898989;font-size:clamp(11px,2vw,15px)}
.card{background:var(--panel);border:1px solid var(--edge);border-radius:19px;padding:22px 24px;margin:16px 0 21px}
h2{font-size:18px;letter-spacing:.24em;color:#e0e0e0;margin:0 0 25px;padding-bottom:14px;border-bottom:1px solid var(--accent)}h3{font-size:15px;color:#ccc;letter-spacing:.12em}
label,.label{display:block;letter-spacing:.13em;font-size:14px;color:#9d9d9d;margin:14px 0 5px}
input,select{background:#070707;color:var(--accent);border:1px solid #545454;border-radius:8px;padding:11px 12px;min-height:48px;width:100%;font-size:17px;outline-offset:2px}
input[type=text].bright{background:#f9f9f9;color:#181818}input:focus,select:focus{border-color:var(--accent)}input[type=checkbox]{width:auto;min-height:auto}
button.action{width:100%;margin:12px 0;padding:13px 10px;min-height:55px;border:2px solid var(--accent);background:transparent;color:var(--accent);border-radius:8px;letter-spacing:.15em;text-transform:uppercase;font-size:15px}
button.action:hover,button.action:focus-visible{background:#2d2111}
button.danger{border-color:#ff4249;color:#ff4249}button.danger:hover{background:#361719}
button.tiny{width:auto;min-width:40px;min-height:36px;border:1px solid #fb535d;color:#fb535d;background:transparent;border-radius:7px}
button.pick{border:1px solid #555;background:#080808;color:#aaa;min-height:47px;border-radius:8px;width:100%}button.pick.active{color:var(--accent);background:#2b1c0b;border-color:var(--accent)}
.flex{display:flex;gap:12px;align-items:flex-end}.flex>*{flex:1;min-width:0}.buttons{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin:7px 0 22px}.two{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}
.note{font-size:14px;color:#999;line-height:1.6}.info{margin:14px 0;padding:12px 14px;color:#aaa;background:#101010;border:1px solid #454545;border-radius:9px;white-space:pre-wrap;overflow-wrap:anywhere}.info.ok{color:#c4efca;border-color:#2a5738}.info.error{color:#ffc9c7;border-color:#8a343a}
#scope{width:100%;max-width:640px;aspect-ratio:1;display:block;margin:0 auto;background:#050b06;cursor:crosshair}
.scopebox{margin:0 auto;max-width:650px}.count{font-size:16px;text-align:center;letter-spacing:.12em;margin:12px 0 17px}.count b{color:var(--accent);font-size:38px;font-weight:400}
.airportRow,.flightRow{display:grid;align-items:center;gap:8px;padding:12px 0;border-bottom:1px solid #383838}.airportRow{grid-template-columns:1fr 1.5fr auto}.flightRow{grid-template-columns:minmax(0,1.2fr) 1fr 1fr auto;text-decoration:none;color:#b1b1b1}.flightRow:hover{color:#eee}.flightRow .call{color:var(--accent);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.hint{font-size:13px;color:#9d9d9d}.under{padding-bottom:14px;border-bottom:1px solid #454545;margin-bottom:18px}.selectColor{appearance:none;min-width:44px;width:44px;height:44px;border-radius:100%;border:0;box-shadow:0 0 0 2px transparent;cursor:pointer}.selectColor.on{box-shadow:0 0 0 3px white,0 0 10px 6px #ffffff23}
.colors{display:flex;gap:19px;align-items:center;margin:10px 0 15px}.radioLine{display:flex;gap:9px;align-items:center}.radioLine input{width:auto;height:auto;min-height:auto}details{margin-top:15px}summary{cursor:pointer;color:var(--accent)}.tag{font-size:12px;border:1px solid #666;border-radius:5px;padding:3px 7px;color:#ccc}.statusLine{margin:5px 0 9px;color:var(--accent);font-size:14px}
@media(max-width:580px){.card{padding:20px 16px}.airportRow{grid-template-columns:1fr 1.5fr auto;font-size:13px}.flightRow{grid-template-columns:1fr 1fr 1fr;font-size:12px}.flightRow span:nth-child(4){display:none}main{padding:8px 9px 45px}h2{font-size:16px}header h1{letter-spacing:.1em}.buttons{gap:7px}}
</style></head>
<body><main><header><h1>✈ FLIGHT RADAR</h1><div class="sub">ESP32 · SkyOrb · ADS-B</div></header>
<section class="card"><h2>LIVE RADAR</h2><div class="scopebox"><canvas id="scope" width="640" height="640" aria-label="Live aircraft radar"></canvas></div>
<p class="count"><b id="count">0</b> aircraft in range</p><p class="note" style="text-align:center">Tap a flight below to look it up on FlightRadar24.</p><div id="flights"></div><div class="info" id="feedStatus">Waiting for aircraft data...</div></section>
<section class="card" id="location"><h2>SEARCH CENTER</h2><label for="address">ADDRESS OR PLACE</label>
<div class="flex"><input id="address" class="bright" type="text" placeholder="City, state or street address" aria-label="Address or place"><button class="pick" id="lookup" type="button" style="flex:0 0 64px">🔍</button></div>
<div class="statusLine" id="geoStatus">Type a location and select search.</div>
<form id="settings"><div class="two"><div><label for="lat">LATITUDE</label><input name="lat" id="lat" type="number" step="any" min="-90" max="90" required placeholder="0.0000"></div>
<div><label for="lon">LONGITUDE</label><input name="lon" id="lon" type="number" step="any" min="-180" max="180" required placeholder="0.0000"></div></div>
<p class="note">If an address isn't found, you can enter coordinates manually (Google Maps shows them when you select a location).</p>
<div class="label">SEARCH RADIUS</div><div class="buttons" id="ranges"><button class="pick" type="button" data-r="5">5</button><button class="pick" type="button" data-r="10">10</button><button class="pick active" type="button" data-r="20">20</button><button class="pick" type="button" data-r="30">30</button></div>
<div class="statusLine" style="text-align:center">MILES</div><input type="hidden" name="radius" id="radius" value="20"><input name="accent" id="accent" type="hidden" value="#ff9200">
<input type="hidden" name="fallback" value="0"><input type="hidden" name="clock24" id="clock24" value="1"><input type="hidden" name="swap" id="swap" value="0" disabled>
<button class="action" type="submit">▶ Apply location</button><div id="saveStatus" class="info">Set a location to start tracking aircraft.</div>
<p class="note">The clock shows UTC until SkyOrb can determine your time zone from the saved location. Daylight saving is refreshed automatically.</p>
<div class="label">CLOCK FORMAT</div><div class="two"><button class="pick active" type="button" id="format24">24-hour</button><button class="pick" type="button" id="format12">12-hour</button></div>
<details><summary>Advanced display options</summary><label class="radioLine"><input type="checkbox" id="swapper"> Swap clock and radar physical screens</label>
<label>Fallback UTC offset (hours)<input id="fallback" type="number" step="0.5" min="-12" max="14" value="0"></label><p class="note">Use only if automatic timezone lookup fails.</p></details>
</form></section>
<section class="card" id="airportsCard"><h2>AIRPORTS</h2><div id="airports"><p class="note">No airport markers saved yet.</p></div>
<button class="action" id="airportAuto" type="button">🌎 Find commercial airports nearby</button>
<button class="action danger" id="airportClear" type="button">× Remove all airports</button>
<div class="under"></div><div class="label">ICAO OR IATA CODE</div><input id="airportCode" type="text" maxlength="4" placeholder="Example: KSEA or SEA">
<button class="action" id="airportByCode" type="button">▶ Find and add airport</button>
<details><summary>Advanced: add airport by coordinates</summary><form id="airportForm"><div class="two"><div><label>ICAO / IATA<input name="name" required maxlength="8" placeholder="KSEA"></label></div><div></div>
<div><label>LATITUDE<input name="lat" type="number" step="any" required></label></div><div><label>LONGITUDE<input name="lon" type="number" step="any" required></label></div></div><button type="submit" class="action">Add airport manually</button></form></details>
<div id="airportStatus" class="info">Airport discovery becomes available after saving a location and connecting to Wi-Fi.</div></section>
<section class="card"><h2>NETWORK</h2><div id="networkStatus" class="info">Checking Wi-Fi connection...</div>
<p><a href="/setup">Configure WiFi / Network scan / Info</a></p><button type="button" id="resetWifi" class="action danger">⚠ Reset Wi-Fi</button>
<p class="note">Reset Wi-Fi clears the network credentials and reopens the SKYORB-Setup hotspot. Other SkyOrb settings are retained.</p>
<details><summary>Firmware updates</summary><p class="note">Set an 8–48 character administrator update password while connected to SKYORB-Setup. Firmware upload requires this password and the username <b>admin</b>.</p>
<form id="adminForm"><label>Current password (only for changing an existing one)<input type="password" name="current" autocomplete="off"></label>
<label>New update password<input type="password" name="newpass" required minlength="8" maxlength="48"></label><button class="action" type="submit">Save update password</button></form>
<a href="/update">Open local firmware-update page</a><div id="adminStatus" class="statusLine"></div></details>
<p class="note">To recover an unreachable device, unplug and replug it; SKYORB-Setup reopens temporarily. Avoid uploading firmware while the device is in use.</p></section>
<section class="card"><h2>APPEARANCE</h2><div class="label">PORTAL THEME COLOR</div><div id="colors" class="colors">
<button class="selectColor on" type="button" data-c="#ff9200" aria-label="Orange" style="background:#ff9200"></button>
<button class="selectColor" type="button" data-c="#1dc653" aria-label="Green" style="background:#1dc653"></button>
<button class="selectColor" type="button" data-c="#2b9bfa" aria-label="Blue" style="background:#2b9bfa"></button>
<button class="selectColor" type="button" data-c="#ff3e43" aria-label="Red" style="background:#ff3e43"></button></div>
<p class="note">Changes the accent color of this control page. The physical radar remains green, just like the original SkyOrb.</p></section>
<section class="card" id="sweepCard"><h2>RADAR SWEEP</h2>
<p class="note">Adjust the physical radar animation to your liking. Zero trail draws a single crisp sweep line; higher values make a longer green glow. Your aircraft, airports and location will not change.</p>
<div class="label">QUICK PRESETS</div>
<div class="two" id="sweepPresets" style="grid-template-columns:repeat(3,minmax(0,1fr));gap:8px">
<button class="pick" type="button" data-trail="0" data-seconds="11" data-brightness="100">Crisp</button>
<button class="pick" type="button" data-trail="50" data-seconds="11" data-brightness="100">Classic</button>
<button class="pick" type="button" data-trail="100" data-seconds="11" data-brightness="100">Glow</button>
</div>
<label for="sweepTrail">TRAIL / GHOSTING <span id="sweepTrailValue" class="tag">50%</span></label>
<input id="sweepTrail" type="range" min="0" max="100" step="5" value="50" style="width:100%;min-height:34px"/>
<div class="flex hint" style="justify-content:space-between"><span>No trail</span><span>Long trail</span></div>
<label for="sweepSeconds">ROTATION SPEED <span id="sweepSecondsValue" class="tag">11 sec / turn</span></label>
<input id="sweepSeconds" type="range" min="5" max="20" step="1" value="11" style="width:100%;min-height:34px"/>
<div class="flex hint" style="justify-content:space-between"><span>Fast</span><span>Slow</span></div>
<label for="sweepBrightness">SWEEP BRIGHTNESS <span id="sweepBrightnessValue" class="tag">100%</span></label>
<input id="sweepBrightness" type="range" min="20" max="100" step="5" value="100" style="width:100%;min-height:34px"/>
<button id="sweepApply" class="action" type="button">Apply radar sweep</button>
<div id="sweepStatus" class="info">Adjust your sweep and press Apply. Changes appear on the physical radar without rebooting.</div>
<p class="note">The display's own refresh can cause artifacts that a software trail setting cannot eliminate completely. This changes the intentional glow, not the hardware refresh rate.</p>
</section>
<footer class="note">SkyOrb community replacement • Flight data: <a href="https://adsb.lol/">ADSB.lol</a> and <a href="https://adsb.fi/">ADSB.fi</a> • Airport/address data: © OpenStreetMap contributors • For hobby use only, not navigation.</footer>
</main><script>
const $=id=>document.getElementById(id),C=$('scope'),g=C.getContext('2d');
let flights=[],airports=[],rad=20,lat=0,lon=0,locSet=false,connected=false,palette='#ff9200';
let last=0,lastStatus='';
let sweepPrefs={trail:50,seconds:11,brightness:100};
function updateSweepControls(trail,seconds,brightness){
  trail=Number(trail);seconds=Number(seconds);brightness=Number(brightness);
  if(!Number.isFinite(trail))trail=50;if(!Number.isFinite(seconds))seconds=11;
  if(!Number.isFinite(brightness))brightness=100;
  trail=Math.max(0,Math.min(100,trail));seconds=Math.max(5,Math.min(20,seconds));
  brightness=Math.max(20,Math.min(100,brightness));
  sweepPrefs={trail,seconds,brightness};
  $('sweepTrail').value=trail;$('sweepSeconds').value=seconds;$('sweepBrightness').value=brightness;
  $('sweepTrailValue').textContent=trail===0?'Off':trail+'%';
  $('sweepSecondsValue').textContent=seconds+' sec / turn';
  $('sweepBrightnessValue').textContent=brightness+'%';
  for(const b of $('sweepPresets').querySelectorAll('button')){
    b.classList.toggle('active',Number(b.dataset.trail)===trail && Number(b.dataset.seconds)===seconds && Number(b.dataset.brightness)===brightness);
  }
}
for(const id of ['sweepTrail','sweepSeconds','sweepBrightness']){
  $(id).addEventListener('input',()=>updateSweepControls($('sweepTrail').value,$('sweepSeconds').value,$('sweepBrightness').value));
}
for(const b of $('sweepPresets').querySelectorAll('button')){
  b.addEventListener('click',()=>updateSweepControls(b.dataset.trail,b.dataset.seconds,b.dataset.brightness));
}
$('sweepApply').onclick=async()=>{
  const button=$('sweepApply');button.disabled=true;msg('sweepStatus','Saving radar sweep...');
  try{
    let res=await api('/radar/sweep',{method:'POST',body:new URLSearchParams({trail:sweepPrefs.trail,seconds:sweepPrefs.seconds,brightness:sweepPrefs.brightness})});
    msg('sweepStatus',res.message||'Sweep updated.');
  }catch(e){msg('sweepStatus','Could not save sweep: '+e.message,true)}
  finally{button.disabled=false;}
};

const safe=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function msg(id,s,err=false){let e=$(id);e.textContent=s;e.classList.toggle('error',!!err);e.classList.toggle('ok',!err);}
async function api(url,options){let r=await fetch(url,options),txt=await r.text();let x;try{x=JSON.parse(txt)}catch(e){throw Error('Unexpected reply (HTTP '+r.status+')')}if(!r.ok||x.error)throw Error(x.error||('HTTP '+r.status));return x;}
function setRange(n){rad=Number(n);$('radius').value=n;for(let b of $('ranges').querySelectorAll('button'))b.classList.toggle('active',Number(b.dataset.r)===rad);}
function setClock(v){$('clock24').value=v?'1':'0';$('format24').classList.toggle('active',v);$('format12').classList.toggle('active',!v);}
function setPalette(v){if(!/^#[a-f\d]{6}$/i.test(v||''))return;palette=v;document.documentElement.style.setProperty('--accent',v);$('accent').value=v;
for(let b of $('colors').querySelectorAll('button'))b.classList.toggle('on',b.dataset.c.toLowerCase()===v.toLowerCase());}
async function config(){try{let d=await api('/api/config');$('lat').value=d.locationSet?d.lat:'';$('lon').value=d.locationSet?d.lon:'';
locSet=!!d.locationSet;lat=d.lat;lon=d.lon;setRange(d.radius||20);setClock(!!d.clock24);$('swapper').checked=!!d.swap;
$('fallback').value=d.fallback??0;setPalette(d.accent||'#ff9200');updateSweepControls(d.sweepTrail??50,d.sweepSeconds??11,d.sweepBrightness??100);airports=d.airports||[];drawAirports();
msg('saveStatus',locSet?'Location saved: '+lat.toFixed(4)+', '+lon.toFixed(4):'No location saved yet. Search an address or enter coordinates, then Apply.');
}catch(e){msg('saveStatus','Could not load settings: '+e.message,true)}}
function drawAirports(){let e=$('airports');e.replaceChildren();if(!airports.length){e.textContent='No airports saved.';return;}for(let i=0;i<airports.length;i++){
let a=airports[i],row=document.createElement('div');row.className='airportRow';let name=document.createElement('span');name.textContent=a.name;
let coords=document.createElement('span');coords.textContent=a.lat.toFixed(3)+', '+a.lon.toFixed(3);
let del=document.createElement('button');del.type='button';del.textContent='×';del.className='tiny';del.ariaLabel='Remove '+a.name;
del.onclick=()=>deleteAirport(i);row.append(name,coords,del);e.append(row);}}
async function deleteAirport(i){try{await api('/airport/delete?i='+i,{method:'POST'});await config();msg('airportStatus','Airport removed.');}catch(e){msg('airportStatus',e.message,true)}}
$('lookup').onclick=async()=>{let q=$('address').value.trim();if(!q){msg('geoStatus','Enter an address or place first.',true);return}
msg('geoStatus','Searching for location...');try{let d=await api('/api/geocode?q='+encodeURIComponent(q));$('lat').value=d.lat.toFixed(5);$('lon').value=d.lon.toFixed(5);
msg('geoStatus','✓ Coordinates found. Press APPLY to save the radar location.');}catch(e){msg('geoStatus','Address lookup unavailable: '+e.message,true)}};
$('address').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();$('lookup').click();}};
for(let b of $('ranges').querySelectorAll('button'))b.onclick=()=>setRange(b.dataset.r);
$('format24').onclick=()=>setClock(true);$('format12').onclick=()=>setClock(false);
$('settings').onsubmit=async e=>{e.preventDefault();let f=new URLSearchParams(new FormData($('settings')));f.set('swap',$('swapper').checked?'1':'0');
// Checkbox is sent only if selected so that the ESP32 can correctly clear it.
if(!$('swapper').checked)f.delete('swap');f.set('fallback',$('fallback').value);
msg('saveStatus','Saving location...');try{let d=await api('/save',{method:'POST',body:f});await config();msg('saveStatus','✓ '+(d.message||'Location saved.'));await refresh();}
catch(e){msg('saveStatus','Save failed: '+e.message,true)}};
$('airportAuto').onclick=async()=>{if(!locSet){msg('airportStatus','Set your radar location first.',true);return}msg('airportStatus','Searching for nearby airports (may take up to 30 seconds)...');
try{await api('/airport/auto',{method:'POST'});
let attempts=0;let timer=setInterval(async()=>{try{let d=await api('/api/live');
if(!d.airportLookupBusy && d.airportLookupResult && d.airportLookupResult!=='Not searched yet'){
clearInterval(timer);await config();let text=d.airportLookupResult;msg('airportStatus',text,/failed|no coded|connect/i.test(text));}
else if(++attempts>=12){clearInterval(timer);msg('airportStatus','Airport search still running; check again shortly.',true);}
}catch(e){clearInterval(timer);msg('airportStatus',e.message,true);}},3000);
}catch(e){msg('airportStatus',e.message,true)}};
$('airportByCode').onclick=async()=>{let code=$('airportCode').value.trim().toUpperCase();if(!/^[A-Z0-9]{3,4}$/.test(code)){msg('airportStatus','Use an IATA or ICAO airport code (3–4 characters).',true);return}
msg('airportStatus','Finding airport '+code+'...');try{let d=await api('/airport/code',{method:'POST',body:new URLSearchParams({code})});await config();msg('airportStatus',d.message||'Airport added.');}
catch(e){msg('airportStatus',e.message,true)}};
$('airportForm').onsubmit=async e=>{e.preventDefault();try{let r=await fetch('/airport/add',{method:'POST',body:new URLSearchParams(new FormData($('airportForm')))});
if(!r.ok)throw Error('Manual airport save failed');await config();msg('airportStatus','Airport saved.');}catch(err){msg('airportStatus',err.message,true)}};
$('airportClear').onclick=async()=>{if(!confirm('Remove all saved airport markers?'))return;try{await api('/airport/clear',{method:'POST'});await config();msg('airportStatus','All airports removed.');}catch(e){msg('airportStatus',e.message,true)}};
$('resetWifi').onclick=async()=>{if(!confirm('Clear your saved Wi-Fi and return to SKYORB-Setup?'))return;try{let r=await fetch('/wifi/reset',{method:'POST'});if(!r.ok)throw Error('Wi-Fi reset failed');msg('networkStatus','Wi-Fi credentials cleared. Connect to SKYORB-Setup (SKYORB1234) and open 192.168.4.1.');}catch(e){msg('networkStatus',e.message,true)}};
$('adminForm').onsubmit=async e=>{e.preventDefault();try{let d=await api('/admin/password',{method:'POST',body:new URLSearchParams(new FormData($('adminForm')))});msg('adminStatus',d.message);$('adminForm').reset();}
catch(err){msg('adminStatus',err.message,true)}};
for(let b of $('colors').querySelectorAll('button'))b.onclick=async()=>{setPalette(b.dataset.c);try{await api('/theme',{method:'POST',body:new URLSearchParams({accent:b.dataset.c})});}
catch(e){msg('networkStatus','Theme could not be saved: '+e.message,true)}};
function frLink(a){let c=(a.flight||'').trim(),r=(a.reg||'').trim();if(/^[A-Z0-9-]+$/i.test(c)&&c.length)return 'https://www.flightradar24.com/data/flights/'+encodeURIComponent(c.toLowerCase());
if(/^[A-Z0-9-]+$/i.test(r)&&r.length)return 'https://www.flightradar24.com/data/aircraft/'+encodeURIComponent(r.toLowerCase());return 'https://www.flightradar24.com/';}
async function refresh(){try{let d=await api('/api/live');last=Date.now();connected=d.wifi==='connected';flights=d.flights||[];airports=d.airports||[];
locSet=!!d.locationSet;lat=d.lat;lon=d.lon;rad=d.radius||20;let net=connected?'CONNECTED • '+d.homeIp:'DISCONNECTED';
msg('networkStatus','Home Wi-Fi: '+net+'\nLocal address: http://'+(d.homeIp||'192.168.4.1')+'\nRecovery setup hotspot: '+(d.setupActive?'active (192.168.4.1)':'off')+'\nTimezone: '+d.timezone);
msg('feedStatus',(d.source&&d.source!=='none'?'Source: '+d.source+' • ':'')+(d.status||'Waiting for aircraft feed')+' • HTTP '+d.httpCode);
$('count').textContent=d.count||0;const list=$('flights');list.replaceChildren();if(!flights.length){list.textContent=locSet?'No aircraft reported right now.':'Choose a radar location below to start tracking.';return;}
for(let a of flights){let row=document.createElement('a');row.className='flightRow';row.href=frLink(a);row.target='_blank';row.rel='noopener noreferrer';let name=document.createElement('span');name.className='call';name.textContent=a.flight||a.hex||'UNKNOWN';
let fl=document.createElement('span');fl.textContent=a.ground?'GROUND':'FL'+String(Math.max(0,Math.round((a.altitude||0)/100))).padStart(3,'0');let speed=document.createElement('span');speed.textContent=Math.round(a.speed||0)+' kts';
let deg=document.createElement('span');deg.textContent=Math.round(a.heading||0)+'°';row.append(name,fl,speed,deg);list.append(row);}
}catch(e){msg('feedStatus','Cannot reach SkyOrb: '+e.message,true)}}
function pos(a){let scale=Math.max(1,rad)*.868976,dx=(a.lon-lon)*60*Math.cos(lat*Math.PI/180),dy=(a.lat-lat)*60;
return [320+dx/scale*281,320-dy/scale*281];}
function sweep(ts){const S=640,R=282,theta=(ts/(sweepPrefs.seconds*1000)*2*Math.PI)%(Math.PI*2);g.fillStyle='#031006';g.fillRect(0,0,S,S);
g.strokeStyle='#14401e';g.lineWidth=2;for(let r of [R/4,R/2,3*R/4,R]){g.beginPath();g.arc(320,320,r,0,2*Math.PI);g.stroke();}
g.lineWidth=1;g.beginPath();g.moveTo(320,38);g.lineTo(320,602);g.moveTo(38,320);g.lineTo(602,320);g.stroke();
for(let j=0;j<12;j++){let angle=j*Math.PI/6;g.beginPath();g.moveTo(320+R*Math.sin(angle),320-R*Math.cos(angle));g.lineTo(320+(R-10)*Math.sin(angle),320-(R-10)*Math.cos(angle));g.stroke();}
g.fillStyle='#24e353';g.font='bold 17px monospace';g.textAlign='center';g.fillText('N',320,49);g.fillText('20 mi'.replace('20',String(rad)),320,609);
let sweepRays=sweepPrefs.trail===0?1:1+Math.round(sweepPrefs.trail*.48);
 let sweepWidth=sweepPrefs.trail*.009;
 for(let k=sweepRays-1;k>=0;k--){
 let progress=sweepRays===1?1:(sweepRays-1-k)/(sweepRays-1);
 let a=theta-(sweepRays===1?0:sweepWidth*k/(sweepRays-1));
 let alpha=(sweepRays===1?1:.10+.90*progress)*(sweepPrefs.brightness/100);
 g.strokeStyle='rgba(28,255,63,'+alpha.toFixed(3)+')';g.lineWidth=3;
 g.beginPath();g.moveTo(320,320);g.lineTo(320+R*Math.sin(a),320-R*Math.cos(a));g.stroke();}
for(let a of airports){let [x,y]=pos(a);if(Math.hypot(x-320,y-320)>R)continue;g.lineWidth=2;g.strokeStyle='#62c8ff';g.beginPath();g.arc(x,y,5,0,2*Math.PI);g.stroke();g.fillStyle='#e2e2e2';g.textAlign='left';g.font='15px monospace';g.fillText(a.name,x+8,y+13);}
for(let a of flights){let [x,y]=pos(a);if(Math.hypot(x-320,y-320)>R)continue;let heading=(a.heading||0)*Math.PI/180;
let ang=Math.atan2(x-320,320-y),diff=Math.abs(Math.atan2(Math.sin(ang-theta),Math.cos(ang-theta)));let col=diff<.25?'#c1ffeb':'#56b7ff';
g.fillStyle=col;g.beginPath();g.moveTo(x+11*Math.sin(heading),y-11*Math.cos(heading));g.lineTo(x-7*Math.sin(heading)+5*Math.cos(heading),y+7*Math.cos(heading)+5*Math.sin(heading));g.lineTo(x-7*Math.sin(heading)-5*Math.cos(heading),y+7*Math.cos(heading)-5*Math.sin(heading));g.closePath();g.fill();
g.textAlign='left';g.font='14px monospace';g.fillStyle='#f1dd83';g.fillText((a.flight||a.hex||'').slice(0,8),x+9,y-6);
g.fillText(a.ground?'GND':'FL'+Math.max(0,Math.round((a.altitude||0)/100)),x+9,y+11);}
requestAnimationFrame(sweep);}
C.addEventListener('click',e=>{let b=C.getBoundingClientRect(),x=(e.clientX-b.left)*640/b.width,y=(e.clientY-b.top)*640/b.height;
let items=flights.map(a=>({a,dist:Math.hypot(pos(a)[0]-x,pos(a)[1]-y)})).sort((a,b)=>a.dist-b.dist);if(items.length&&items[0].dist<25)window.open(frLink(items[0].a),'_blank','noopener');});
config().then(refresh);setInterval(refresh,7000);requestAnimationFrame(sweep);
</script></body></html>
)SKYPORTAL";

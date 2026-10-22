/*
 * SkyOrb Replacement — dual GC9A01 ESP32 radar and clock
 * SPDX-License-Identifier: MIT
 * Original SkyOrb design by GulfCoastMaker. This is a new, independent firmware.
 * Personal, non-commercial use; comply with each upstream data provider's terms.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Update.h>
#include "web_ui.h"

// Original SkyOrb pinout; the shared SPI bus is SCK=18, MOSI=23.
static constexpr int PIN_RADAR_CS=5, PIN_RADAR_DC=2, PIN_RADAR_RST=4;
static constexpr int PIN_CLOCK_CS=16, PIN_CLOCK_DC=17, PIN_CLOCK_RST=21;
static constexpr uint8_t MAX_AIRCRAFT=70, MAX_AIRPORTS=16;
static constexpr uint32_t FETCH_PERIOD=30000;       // 30 s, be polite to public APIs
static constexpr uint32_t TZ_PERIOD=6UL*3600UL*1000UL;
static constexpr float DEG2RAD=0.017453292519943295f;
static constexpr float MILES_TO_NM=0.868976f;

Adafruit_GC9A01A radarPhysical(PIN_RADAR_CS, PIN_RADAR_DC, PIN_RADAR_RST);
Adafruit_GC9A01A clockPhysical(PIN_CLOCK_CS, PIN_CLOCK_DC, PIN_CLOCK_RST);
// Keep the static radar scene directly on its TFT. An old sweep is erased
// in place and the static geometry is redrawn over its former pixels.
// Avoid a 115,200-byte full-screen framebuffer; it can exhaust ESP32 heap,
// silently disabling the radar sweep and starving HTTPS lookups.
WebServer server(80);
DNSServer dns;
Preferences prefs;
SemaphoreHandle_t guard;
bool apMode=false;
bool pendingWifiConnect=false;
bool pendingRestart=false;
uint32_t restartAfter=0;
bool otaUploadOK=false;
String otaError;
bool servicesReady=false;
static constexpr uint32_t AP_GRACE_PERIOD=15UL*60UL*1000UL; // 15-minute recovery window after restart
volatile bool askTZ=false, askAirports=false, askFlights=true, shouldSave=false;
volatile bool radarSceneDirty=true; // Rebuild static radar only when flight or configuration data changes.
bool airportLookupBusy=false;
String airportLookupResult="Not searched yet";
void airportProgress(bool busy,const String &message){
  xSemaphoreTake(guard,portMAX_DELAY);
  airportLookupBusy=busy;airportLookupResult=message;
  xSemaphoreGive(guard);
}

struct Airport {String name; float lat=0, lon=0;};
struct Aircraft {
  String flight, hex, reg;
  float lat=0, lon=0, heading=0, speed=0;
  int altitude=0;
  bool ground=false;
};
struct Config {
  String ssid, password, tzName="UTC", tzAbbr="UTC", accent="#ff9200", otaPassword="";
  float lat=0, lon=0;
  int radius=20;
  bool locationSet=false, clock24=false, swapScreens=false;
  float fallback=0;                 // UTC offset hours (only if lookup fails)
  int utcOffset=0;                 // derived offset, refreshed for DST
  Airport airports[MAX_AIRPORTS];
  int airportCount=0;
};
Config cfg;
struct RadarData {
  Aircraft flights[MAX_AIRCRAFT];
  int count=0, httpCode=0;
  uint32_t updatedAt=0;
  String source="none", status="Waiting for network";
};
RadarData live;

static uint16_t green=0x0600, lowgreen=0x01c3, pale=0xd7ff, amber=0xfd28;
static Adafruit_GC9A01A &clockScreen(const Config &c){return c.swapScreens?radarPhysical:clockPhysical;}
static Adafruit_GC9A01A &radarScreen(const Config &c){return c.swapScreens?clockPhysical:radarPhysical;}

String cleanName(String s, uint8_t maxLen=10){s.trim(); s.toUpperCase();if(s.length()>maxLen)s.remove(maxLen);return s;}
String urlEncode(const String &s){const char hex[]="0123456789ABCDEF";String v;v.reserve(s.length()*3);for(size_t i=0;i<s.length();++i){uint8_t c=(uint8_t)s[i];if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')v+=(char)c;else{v+='%';v+=hex[c>>4];v+=hex[c&15];}}return v;}
float clampF(float v,float a,float b){return v<a?a:(v>b?b:v);}
int clampI(int v,int a,int b){return v<a?a:(v>b?b:v);}
Config copyCfg(){xSemaphoreTake(guard,portMAX_DELAY);Config c=cfg;xSemaphoreGive(guard);return c;}
void savePrefs(){
  Config c=copyCfg();
  prefs.putString("ssid",c.ssid);prefs.putString("pass",c.password);
  prefs.putBool("hasLoc",c.locationSet);prefs.putFloat("lat",c.lat);prefs.putFloat("lon",c.lon);
  prefs.putInt("rad",c.radius);prefs.putBool("24h",c.clock24);prefs.putBool("swap",c.swapScreens);
  prefs.putFloat("fallback",c.fallback);prefs.putString("accent",c.accent);
  prefs.putString("tz",c.tzName);prefs.putString("abbr",c.tzAbbr);prefs.putInt("offset",c.utcOffset);
  prefs.putString("otaPass",c.otaPassword);
  JsonDocument d;JsonArray a=d.to<JsonArray>();
  for(int i=0;i<c.airportCount;i++){JsonObject o=a.add<JsonObject>();o["name"]=c.airports[i].name;o["lat"]=c.airports[i].lat;o["lon"]=c.airports[i].lon;}
  String json;serializeJson(d,json);prefs.putString("airports",json);
  shouldSave=false;
}
void loadPrefs(){
  prefs.begin("skyorb-v2",false);
  cfg.ssid=prefs.getString("ssid","");cfg.password=prefs.getString("pass","");
  cfg.locationSet=prefs.getBool("hasLoc",false);cfg.lat=prefs.getFloat("lat",0);cfg.lon=prefs.getFloat("lon",0);
  cfg.radius=clampI(prefs.getInt("rad",20),5,30);
  cfg.clock24=prefs.getBool("24h",false);cfg.swapScreens=prefs.getBool("swap",false);
  cfg.fallback=prefs.getFloat("fallback",0);cfg.accent=prefs.getString("accent","#ff9200");
  cfg.tzName=prefs.getString("tz","UTC");cfg.tzAbbr=prefs.getString("abbr","UTC");cfg.utcOffset=prefs.getInt("offset",0);
  cfg.otaPassword=prefs.getString("otaPass","");
  String airportsJson=prefs.getString("airports","[]");JsonDocument d;
  if(!deserializeJson(d,airportsJson)){
    for(JsonObject a:d.as<JsonArray>()){
      if(cfg.airportCount>=MAX_AIRPORTS)break;
      Airport &v=cfg.airports[cfg.airportCount++];v.name=cleanName(a["name"].as<String>(),8);
      v.lat=a["lat"].as<float>();v.lon=a["lon"].as<float>();
    }
  }
}

// Run the access point and station together so the setup page remains usable
// while SkyOrb joins home Wi-Fi and performs address/airport lookups.
// Once fully configured and connected, the setup AP closes after 15 minutes.
// Power-cycling the ESP32 opens a new recovery window.
void startSetupAP(){
  if(apMode)return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  if(!WiFi.softAP("SKYORB-Setup","SKYORB1234")){
    Serial.println("[WiFi] Failed to start setup hotspot");return;
  }
  dns.start(53,"*",WiFi.softAPIP());
  apMode=true;
  Serial.printf("[WiFi] Setup/recovery AP http://%s\n",WiFi.softAPIP().toString().c_str());
}
void startWifiConnection(){
  Config c=copyCfg();
  if(!c.ssid.length())return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.begin(c.ssid.c_str(),c.password.c_str());
  Serial.printf("[WiFi] Connecting to SSID: %s\n",c.ssid.c_str());
}

bool getJsonHTTPS(const String &url,JsonDocument &doc,int &code,JsonDocument *filter=nullptr){
  WiFiClientSecure client;
  // Prototype tradeoff: no root CA certificate bundle on this small ESP32.
  // This encrypts traffic but does not authenticate the HTTPS server identity.
  client.setInsecure();client.setTimeout(10);
  HTTPClient http;http.setTimeout(11000);http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(client,url)){code=-99;return false;}
  http.addHeader("User-Agent","SkyOrb-Community/1.0 (+https://github.com/GulfCoastMaker/SkyOrb)");
  http.addHeader("Accept-Encoding","identity");
  code=http.GET();
  if(code==HTTP_CODE_OK){
    String body=http.getString(); // max search radius 30mi keeps responses modest
    DeserializationError err=filter?deserializeJson(doc,body,DeserializationOption::Filter(*filter)):deserializeJson(doc,body);
    http.end();if(err){Serial.printf("[HTTP] JSON: %s (%s)\n",err.c_str(),url.c_str());code=-98;return false;}
    return true;
  }
  Serial.printf("[HTTP] %d %s\n",code,url.c_str());http.end();return false;
}

// Prefer a documented public Overpass mirror. The older default instance
// frequently refuses automated requests, and TLS failure (-1) is not an
// indication that there are no airports near the selected location.
bool getOverpassJson(const String &query,JsonDocument &doc,int &lastCode){
  const String encoded=urlEncode(query);
  const char *hosts[]={
    "https://overpass.private.coffee/api/interpreter?data=",
    "https://lz4.overpass-api.de/api/interpreter?data="
  };
  for(const char *host:hosts){
    int status=0;
    doc.clear();
    if(getJsonHTTPS(String(host)+encoded,doc,status)){lastCode=200;return true;}
    lastCode=status;
    Serial.printf("[Airport] Overpass endpoint failed (%d): %s\n",status,host);
  }
  return false;
}

void refreshTZ(){
  Config c=copyCfg();if(!c.locationSet||WiFi.status()!=WL_CONNECTED)return;
  String url="https://api.open-meteo.com/v1/forecast?latitude="+String(c.lat,5)+"&longitude="+String(c.lon,5)+"&current=temperature_2m&timezone=auto&forecast_days=1";
  JsonDocument d;int status=0;
  if(getJsonHTTPS(url,d,status)){
    String name=d["timezone"]|"";String abbr=d["timezone_abbreviation"]|"";int offset=d["utc_offset_seconds"]|0;
    if(name.length()){
      xSemaphoreTake(guard,portMAX_DELAY);cfg.tzName=name;cfg.tzAbbr=abbr.length()?abbr:"LOCAL";cfg.utcOffset=offset;xSemaphoreGive(guard);
      shouldSave=true;
      Serial.printf("[Clock] %s, offset %+d seconds\n",name.c_str(),offset);return;
    }
  }
  // Manual fallback only if we have never successfully resolved a zone.
  xSemaphoreTake(guard,portMAX_DELAY);
  if(cfg.tzName=="UTC"||cfg.tzName=="UTC fallback"){
    cfg.utcOffset=roundf(cfg.fallback*3600);cfg.tzName="UTC fallback";cfg.tzAbbr="UTC";
  }
  xSemaphoreGive(guard);
}

// Auto-discover coded aerodromes from OSM; this is user-triggered or one time
// after location is saved, not a repeated polling service.
void discoverAirports(){
  Config c=copyCfg();
  if(!c.locationSet){airportProgress(false,"Set radar location before searching for airports.");return;}
  if(WiFi.status()!=WL_CONNECTED){airportProgress(false,"Connect SkyOrb to home Wi-Fi first.");return;}
  airportProgress(true,"Searching nearby airports...");
  int searchM=(int)(c.radius*1609.344f);
  // Include ICAO-only airports as well as those with 3-letter IATA codes.
  String around="(around:"+String(searchM)+","+String(c.lat,5)+","+String(c.lon,5)+")";
  String query="[out:json][timeout:12];(nwr"+around+"[\"aeroway\"=\"aerodrome\"][\"iata\"];"
               "nwr"+around+"[\"aeroway\"=\"aerodrome\"][\"icao\"];);out center 75;";
  JsonDocument doc;int code=0;
  if(!getOverpassJson(query,doc,code)){
    airportProgress(false,"Airport discovery failed (HTTP "+String(code)+"). Check network or retry later.");
    Serial.printf("[Airports] All endpoints failed: %d\n",code);
    return;
  }
  Airport found[MAX_AIRPORTS];int n=0;
  for(JsonObject e:doc["elements"].as<JsonArray>()){
    if(n>=MAX_AIRPORTS)break;
    String name=cleanName(e["tags"]["iata"].as<String>(),8);
    if(!name.length())name=cleanName(e["tags"]["icao"].as<String>(),8);
    if(!name.length())continue;
    float a=e["lat"].isNull()?e["center"]["lat"].as<float>():e["lat"].as<float>();
    float o=e["lon"].isNull()?e["center"]["lon"].as<float>():e["lon"].as<float>();
    if(a<-90||a>90||o<-180||o>180||(!a&&!o))continue;
    bool duplicate=false;for(int j=0;j<n;j++)if(found[j].name==name)duplicate=true;
    if(duplicate)continue;
    found[n].name=name;found[n].lat=a;found[n].lon=o;n++;
  }
  if(n){
    xSemaphoreTake(guard,portMAX_DELAY);
    cfg.airportCount=n;for(int i=0;i<n;i++)cfg.airports[i]=found[i];
    xSemaphoreGive(guard);shouldSave=true;radarSceneDirty=true;
    airportProgress(false,"Found "+String(n)+" nearby airports.");
  }else{
    airportProgress(false,"No coded airports found within "+String(c.radius)+" miles. Try a larger radius or manual addition.");
  }
  Serial.printf("[Airports] Found %d airport markers\n",n);
}

// Both providers expose readsb-style aircraft fields. ADSB.fi explicitly
// requests /v3/lat/.../lon/.../dist/... for NEW integrations.
void fetchFlights(){
  Config c=copyCfg();if(!c.locationSet||WiFi.status()!=WL_CONNECTED)return;
  int nauticalMiles=clampI((int)ceilf(c.radius*MILES_TO_NM),5,250);
  String loc=String(c.lat,5)+"/"+String(c.lon,5)+"/"+String(nauticalMiles);
  String urls[2]={"https://api.adsb.lol/v2/point/"+loc,
       "https://opendata.adsb.fi/api/v3/lat/"+String(c.lat,5)+"/lon/"+String(c.lon,5)+"/dist/"+String(nauticalMiles)};
  String providers[2]={"ADSB.lol","ADSB.fi"};
  JsonDocument filter;
  const char* keys[]={"hex","flight","r","lat","lon","alt_baro","track","gs","seen_pos"};
  for(const char *key:keys){filter["ac"][0][key]=true;filter["aircraft"][0][key]=true;}
  int lastCode=0;
  for(int source=0;source<2;source++){
    JsonDocument doc;int code=0;
    if(!getJsonHTTPS(urls[source],doc,code,&filter)){lastCode=code;continue;}
    JsonArray ac=doc["ac"].as<JsonArray>();
    if(ac.isNull())ac=doc["aircraft"].as<JsonArray>(); // provider schema fallback
    if(ac.isNull()){lastCode=-97;continue;}
    Aircraft tmp[MAX_AIRCRAFT];int count=0;
    for(JsonObject a:ac){
      if(count>=MAX_AIRCRAFT)break;
      if(a["lat"].isNull()||a["lon"].isNull())continue;
      if((a["seen_pos"]|0.0f)>60)continue; // skip stale positions
      float latitude=a["lat"].as<float>(), longitude=a["lon"].as<float>();
      if(latitude<-90||latitude>90||longitude<-180||longitude>180)continue;
      float dy=(latitude-c.lat)*60.0f, dx=(longitude-c.lon)*60.0f*cosf(c.lat*DEG2RAD);
      if(sqrtf(dx*dx+dy*dy)>c.radius*MILES_TO_NM)continue;
      Aircraft &p=tmp[count++];
      p.lat=latitude;p.lon=longitude;
      p.flight=cleanName(a["flight"].as<String>(),12);
      p.hex=cleanName(a["hex"].as<String>(),10);
      p.reg=cleanName(a["r"].as<String>(),12);
      p.heading=a["track"]|0.0f;p.speed=a["gs"]|0.0f;
      if(a["alt_baro"].is<const char*>()){
        String s=a["alt_baro"].as<String>();p.ground=(s=="ground");p.altitude=s.toInt();
      }else{p.altitude=a["alt_baro"]|0;}
    }
    xSemaphoreTake(guard,portMAX_DELAY);
    live.count=count;for(int i=0;i<count;i++)live.flights[i]=tmp[i];
    live.updatedAt=millis();live.httpCode=code;live.source=providers[source];
    live.status=count?"Tracking aircraft":"Connected; no aircraft currently in range";
    xSemaphoreGive(guard);
    radarSceneDirty=true;
    Serial.printf("[Flights] %s: %d nearby aircraft\n",providers[source].c_str(),count);
    return;
  }
  xSemaphoreTake(guard,portMAX_DELAY);
  live.count=0;live.httpCode=lastCode;live.source="none";
  live.status="Flight-data request failed (see Serial / API status)";
  xSemaphoreGive(guard);
  radarSceneDirty=true;
}

void worker(void*){
  uint32_t lastFlights=0,lastTZ=0;
  for(;;){
    if(WiFi.status()==WL_CONNECTED){
      if(askTZ || (lastTZ==0 && copyCfg().locationSet) || (lastTZ && millis()-lastTZ>TZ_PERIOD)){
        askTZ=false;refreshTZ();lastTZ=millis();
      }
      if(askAirports){askAirports=false;discoverAirports();}
      if(askFlights || !lastFlights || millis()-lastFlights>=FETCH_PERIOD){
        askFlights=false;fetchFlights();lastFlights=millis();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void replyJson(JsonDocument &d,int code=200){String out;serializeJson(d,out);server.send(code,"application/json",out);}
void errorJson(const String &msg,int code=400){JsonDocument d;d["error"]=msg;replyJson(d,code);}
void redirectRoot(){server.sendHeader("Location","/",true);server.send(303,"text/plain","Saved; return to SkyOrb");}
void publishConfig(){
  Config c=copyCfg();JsonDocument d;
  d["ssid"]=c.ssid;d["lat"]=c.lat;d["lon"]=c.lon;d["radius"]=c.radius;
  d["locationSet"]=c.locationSet;d["clock24"]=c.clock24;d["swap"]=c.swapScreens;
  d["accent"]=c.accent;d["timezone"]=c.tzName;d["timezoneAbbr"]=c.tzAbbr;d["fallback"]=c.fallback;d["otaEnabled"]=c.otaPassword.length()>=8;
  JsonArray a=d["airports"].to<JsonArray>();
  for(int i=0;i<c.airportCount;i++){
    JsonObject o=a.add<JsonObject>();o["name"]=c.airports[i].name;
    o["lat"]=c.airports[i].lat;o["lon"]=c.airports[i].lon;
  }
  replyJson(d);
}
void publishLive(){
  Config c=copyCfg();JsonDocument d;
  d["lat"]=c.lat;d["lon"]=c.lon;d["radius"]=c.radius;
  d["locationSet"]=c.locationSet;d["timezone"]=c.tzName;
  d["wifi"]=WiFi.status()==WL_CONNECTED?"connected":"connecting / disconnected";
  d["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();
  d["homeIp"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"";
  d["setupIp"]=apMode?WiFi.softAPIP().toString():"";
  d["setupActive"]=apMode;d["buffered"]=false;
  d["sweepMode"]="direct-incremental";
  xSemaphoreTake(guard,portMAX_DELAY);
  d["airportLookupBusy"]=airportLookupBusy;
  d["airportLookupResult"]=airportLookupResult;
  xSemaphoreGive(guard);
  JsonArray ar=d["airports"].to<JsonArray>();
  for(int i=0;i<c.airportCount;i++){
    JsonObject o=ar.add<JsonObject>();o["name"]=c.airports[i].name;o["lat"]=c.airports[i].lat;o["lon"]=c.airports[i].lon;
  }
  xSemaphoreTake(guard,portMAX_DELAY);
  bool stale=live.updatedAt && millis()-live.updatedAt>150000UL;
  d["count"]=stale?0:live.count;
  d["status"]=!c.locationSet?"Set your location to start":(stale?"Flight data stale; checking connection":live.status);
  d["source"]=live.source;d["httpCode"]=live.httpCode;
  d["ageSeconds"]=live.updatedAt?(int)((millis()-live.updatedAt)/1000):-1;
  JsonArray flights=d["flights"].to<JsonArray>();
  for(int i=0;i<(stale?0:live.count);i++){
    const Aircraft &p=live.flights[i];JsonObject a=flights.add<JsonObject>();
    a["flight"]=p.flight;a["hex"]=p.hex;a["reg"]=p.reg;a["lat"]=p.lat;a["lon"]=p.lon;
    a["heading"]=p.heading;a["speed"]=p.speed;a["altitude"]=p.altitude;a["ground"]=p.ground;
  }
  xSemaphoreGive(guard);
  replyJson(d);
}
void onSave(){
  if(!server.hasArg("lat")||!server.hasArg("lon")){errorJson("Latitude and longitude required");return;}
  float lat=server.arg("lat").toFloat(),lon=server.arg("lon").toFloat();
  if(lat<-90||lat>90||lon<-180||lon>180){errorJson("Invalid coordinates");return;}
  String accent=server.arg("accent");
  if(accent.length()!=7||accent[0]!='#')accent="#ff9200";
  Config prev=copyCfg();bool moved=!prev.locationSet||fabsf(prev.lat-lat)>0.0001f||fabsf(prev.lon-lon)>0.0001f;
  xSemaphoreTake(guard,portMAX_DELAY);
  cfg.lat=lat;cfg.lon=lon;cfg.radius=clampI(server.arg("radius").toInt(),5,30);
  cfg.clock24=server.arg("clock24")=="1";cfg.swapScreens=server.hasArg("swap");
  cfg.accent=accent;cfg.fallback=clampF(server.arg("fallback").toFloat(),-12,14);
  cfg.locationSet=true;
  if(moved){cfg.tzName="UTC fallback";cfg.tzAbbr="UTC";cfg.utcOffset=roundf(cfg.fallback*3600);cfg.airportCount=0;}
  xSemaphoreGive(guard);
  savePrefs();radarSceneDirty=true;askFlights=true;askTZ=true;if(moved)askAirports=true;
  JsonDocument d;d["ok"]=true;
  d["message"]="Location saved. The radar will request nearby flights; airports and timezone update in the background.";
  replyJson(d);
}
void onGeocode(){
  String q=server.arg("q");q.trim();
  if(q.length()<3||q.length()>150){errorJson("Enter a real address or city (3–150 characters)");return;}
  // User initiated only; never auto-completes or polls Nominatim.
  // OSMF Nominatim: single identified request, no more than once per second.
  static String lastQuery="";static uint32_t lastAt=0;static float lastLat=0,lastLon=0;
  if(q==lastQuery && lastAt && millis()-lastAt<3600000){
    JsonDocument d;d["lat"]=lastLat;d["lon"]=lastLon;replyJson(d);return;
  }
  if(lastAt && millis()-lastAt<1100){errorJson("Please wait a second before another address lookup",429);return;}
  if(WiFi.status()!=WL_CONNECTED){errorJson("Home Wi-Fi is not connected yet. Save Wi-Fi in Step 1, wait for Connected status, then try Find address.",503);return;}
  lastAt=millis();
  String url="https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&q="+urlEncode(q);
  JsonDocument d;int code=0;
  if(!getJsonHTTPS(url,d,code)){errorJson("OpenStreetMap address lookup failed (HTTP "+String(code)+"). Check internet access or enter coordinates manually.",502);return;}
  JsonArray result=d.as<JsonArray>();if(result.isNull()||result.size()==0){errorJson("No address found; use coordinates instead",404);return;}
  lastLat=result[0]["lat"].as<float>();lastLon=result[0]["lon"].as<float>();lastQuery=q;
  JsonDocument reply;reply["lat"]=lastLat;reply["lon"]=lastLon;replyJson(reply);
}
void onWifi(){
  String ssid=server.arg("ssid");ssid.trim();String pw=server.arg("password");
  if(!ssid.length()||ssid.length()>32||pw.length()>63){errorJson("Invalid Wi-Fi credentials");return;}
  xSemaphoreTake(guard,portMAX_DELAY);cfg.ssid=ssid;cfg.password=pw;xSemaphoreGive(guard);
  savePrefs();
  JsonDocument d;d["ok"]=true;
  d["message"]="Wi-Fi saved. SkyOrb is connecting now. Stay on SKYORB-Setup at 192.168.4.1 until you save your radar location.";
  replyJson(d);
  // Defer reconnection until after the HTTP response has been sent.
  pendingWifiConnect=true;
}
void onWifiReset(){
  xSemaphoreTake(guard,portMAX_DELAY);cfg.ssid="";cfg.password="";xSemaphoreGive(guard);
  savePrefs();server.send(200,"text/plain","Wi-Fi cleared; setup hotspot will remain available at 192.168.4.1");
  pendingWifiConnect=false;
  WiFi.disconnect(false,false); // Keep SkyOrb-Setup accessible.
  if(!apMode)startSetupAP();
}
// Look up an IATA or ICAO airport code near the saved radar center.
// The browser only needs a code; coordinates are discovered via OSM Overpass.
void onAirportCode(){
  Config c=copyCfg();
  if(!c.locationSet){errorJson("Save your radar location first.");return;}
  if(WiFi.status()!=WL_CONNECTED){errorJson("Connect to home Wi-Fi in Step 1 before searching airports.",503);return;}
  String code=cleanName(server.arg("code"),4);
  if(code.length()<3||code.length()>4){errorJson("Use a three-letter IATA or four-letter ICAO airport code.");return;}
  for(size_t i=0;i<code.length();i++)if(!isalnum((unsigned char)code[i])){errorJson("Airport code must contain only letters and numbers.");return;}
  String key=code.length()==3?"iata":"icao";
  String query="[out:json][timeout:14];nwr(around:250000,"+String(c.lat,5)+","+String(c.lon,5)+")[\"aeroway\"=\"aerodrome\"][\""+key+"\"=\""+code+"\"];out center 1;";
  JsonDocument doc;int status=0;
  if(!getOverpassJson(query,doc,status)){
    errorJson("Airport lookup service unavailable (HTTP " +String(status)+"). Please retry later, or add coordinates manually.",502);return;
  }
  JsonArray matches=doc["elements"].as<JsonArray>();
  if(matches.isNull()||matches.size()==0){errorJson("No airport found with that code near your radar location. Try Auto-find nearby airports.",404);return;}
  JsonObject item=matches[0];
  float lat=item["lat"].isNull()?item["center"]["lat"].as<float>():item["lat"].as<float>();
  float lon=item["lon"].isNull()?item["center"]["lon"].as<float>():item["lon"].as<float>();
  if(lat<-90||lat>90||lon<-180||lon>180){errorJson("Airport coordinates were unavailable.",502);return;}
  xSemaphoreTake(guard,portMAX_DELAY);
  for(int i=0;i<cfg.airportCount;i++){
    if(cfg.airports[i].name==code){xSemaphoreGive(guard);errorJson("That airport is already saved.",409);return;}
  }
  if(cfg.airportCount>=MAX_AIRPORTS){xSemaphoreGive(guard);errorJson("Airport list is full.");return;}
  Airport &a=cfg.airports[cfg.airportCount++];a.name=code;a.lat=lat;a.lon=lon;
  xSemaphoreGive(guard);savePrefs();radarSceneDirty=true;
  JsonDocument response;response["ok"]=true;response["message"]=String("Airport ")+code+" added.";
  replyJson(response);
}
void onAirportAdd(){
  String name=cleanName(server.arg("name"),8);
  float lat=server.arg("lat").toFloat(),lon=server.arg("lon").toFloat();
  if(!name.length()||lat<-90||lat>90||lon<-180||lon>180){errorJson("Bad airport fields");return;}
  xSemaphoreTake(guard,portMAX_DELAY);
  if(cfg.airportCount>=MAX_AIRPORTS){xSemaphoreGive(guard);errorJson("Airport list full");return;}
  Airport &a=cfg.airports[cfg.airportCount++];a.name=name;a.lat=lat;a.lon=lon;
  xSemaphoreGive(guard);savePrefs();radarSceneDirty=true;redirectRoot();
}
void onAirportDelete(){int i=server.arg("i").toInt();xSemaphoreTake(guard,portMAX_DELAY);
  if(i<0||i>=cfg.airportCount){xSemaphoreGive(guard);errorJson("No such airport");return;}
  for(int j=i;j<cfg.airportCount-1;j++)cfg.airports[j]=cfg.airports[j+1];cfg.airportCount--;
  xSemaphoreGive(guard);savePrefs();radarSceneDirty=true;JsonDocument d;d["ok"]=true;replyJson(d);
}
// Captive-portal pages are deliberately separate from the main radar dashboard.
// The AP uses 192.168.4.1; once connected to home Wi-Fi the numeric home IP
// works even when the router does not support .local mDNS names.
static const char SETUP_HTML[] PROGMEM = R"SETPAGE(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SkyOrb — Connect Wi-Fi</title><style>
*{box-sizing:border-box}body{font:16px/1.5 system-ui;background:#101010;color:#eaeaea;max-width:680px;padding:22px;margin:0 auto}
h1{color:#ff9500;font-weight:650}a{color:#ff9500}nav{display:flex;gap:10px;flex-wrap:wrap}nav a{padding:9px 11px;border:1px solid #ff9500;border-radius:7px;text-decoration:none}
section{margin:18px 0;padding:20px;border-radius:15px;background:#1b1b1b;border:1px solid #383838}
label{display:block;margin:12px 0 4px}input{width:100%;padding:12px;font-size:16px;border-radius:7px;border:1px solid #777}
input[type=checkbox]{width:auto}button{background:#ff9500;border:0;border-radius:8px;padding:12px 17px;font-weight:700;cursor:pointer;margin:8px 6px 0 0}
.net{display:block;color:#fff;background:#2e2e2e;border:1px solid #555;text-align:left;width:100%;font-weight:500}
small,#status{color:#bcbcbc;white-space:pre-wrap}#status{padding:12px;border:1px solid #555;border-radius:8px;margin-top:14px}
</style></head><body><h1>✈ SkyOrb Setup</h1>
<nav><a href="#wifi">Configure WiFi</a><a href="#info">Info</a><a href="/">Radar settings</a><a href="/update">Update</a><a href="/">Exit</a></nav>
<section id="wifi"><h2>Configure WiFi</h2><p>Choose your home network or enter its name. Only 2.4 GHz networks are supported by ESP32-WROOM-32.</p>
<button id="scan" type="button">Scan for networks</button><div id="nets"></div>
<form id="frm"><label>Wi-Fi network name (SSID)<input id="ssid" name="ssid" maxlength="32" required autocomplete="off"></label>
<label>Password<input id="pw" name="password" type="password" maxlength="63" autocomplete="new-password"></label>
<label><input id="show" type="checkbox"> Show password</label><button type="submit">Save and connect</button></form>
<div id="status">Connect to SKYORB-Setup and use http://192.168.4.1, or connect from your home network using SkyOrb's displayed IP address.</div>
</section><section id="info"><h2>Info</h2><div id="details">Loading…</div><p><a href="/">Open radar, location, airports &amp; clock settings</a></p>
<p><a href="/update">Upload firmware update</a> (requires an administrator password configured in the dashboard).</p></section>
<script>
const $=id=>document.getElementById(id);
async function api(u,o){let r=await fetch(u,o),t=await r.text(),v;try{v=JSON.parse(t)}catch(e){throw Error('Unexpected device response: '+r.status)}if(!r.ok||v.error)throw Error(v.error||'HTTP '+r.status);return v;}
$('scan').onclick=async()=>{let b=$('scan');b.disabled=true;$('nets').textContent='Scanning…';try{let d=await api('/wifi/scan');$('nets').replaceChildren();if(!d.networks.length)$('nets').textContent='No 2.4 GHz networks found. You can enter your SSID manually.';for(let n of d.networks){let x=document.createElement('button');x.type='button';x.className='net';x.textContent=n.ssid+' ('+n.rssi+' dBm) '+(n.secure?'🔒':'');x.onclick=()=>{$('ssid').value=n.ssid;$('pw').focus();};$('nets').append(x);}}catch(e){$('nets').textContent='Scan failed: '+e.message;}finally{b.disabled=false;}};
$('show').onchange=()=>{$('pw').type=$('show').checked?'text':'password'};
$('frm').onsubmit=async e=>{e.preventDefault();$('status').textContent='Saving…';try{let d=await api('/wifi',{method:'POST',body:new URLSearchParams(new FormData($('frm')))});$('status').textContent=d.message+'\nAfter home Wi-Fi is connected, open the radar settings below to set your location.';setTimeout(status,4000);}catch(err){$('status').textContent=err.message;}};
async function status(){try{let d=await api('/api/live');$('details').textContent='Home Wi-Fi: '+d.wifi+'\nHome IP: '+(d.homeIp||'Not assigned yet')+'\nSetup hotspot: '+(d.setupActive?'192.168.4.1':'Off')+'\nLocation: '+(d.locationSet?'Saved':'Not set');}catch(e){$('details').textContent=e.message;}}
status();setInterval(status,10000);$('scan').click();
</script></body></html>
)SETPAGE";

void showSetup(){server.send_P(200,"text/html",SETUP_HTML);}
void captiveRedirect(){server.sendHeader("Location","http://192.168.4.1/setup",true);server.send(302,"text/plain","");}
void scanWifi(){
  JsonDocument d;JsonArray a=d["networks"].to<JsonArray>();
  int total=WiFi.scanNetworks(false,true,false,300);
  if(total<0){errorJson("Wi-Fi scan failed. Try again.",503);return;}
  for(int i=0;i<total && i<24;i++){
    String name=WiFi.SSID(i);if(name.length()==0)continue;
    bool dupe=false;for(JsonObject o:a)if(o["ssid"].as<String>()==name)dupe=true;
    if(dupe)continue;
    JsonObject n=a.add<JsonObject>();n["ssid"]=name;n["rssi"]=WiFi.RSSI(i);
    n["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();replyJson(d);
}

bool otaAuthorized(){
  Config c=copyCfg();
  return c.otaPassword.length()>=8 && server.authenticate("admin",c.otaPassword.c_str());
}
void requestOtaAuth(){
  if(copyCfg().otaPassword.length()<8){server.send(403,"text/plain","Set an 8+ character update password in the SkyOrb dashboard first.");return;}
  server.requestAuthentication(BASIC_AUTH,"SkyOrb firmware update");
}
static const char OTA_HTML[] PROGMEM=R"OTAPAGE(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="utf-8"><title>SkyOrb firmware update</title>
<style>body{background:#101010;color:#eee;font:16px system-ui;max-width:620px;margin:8vh auto;padding:20px}h1{color:#ff9500}main{background:#202020;border:1px solid #444;padding:25px;border-radius:15px}input,button{font:inherit;padding:11px;margin:9px 0}button{background:#ff9500;border:0;border-radius:8px;cursor:pointer}a{color:#ff9500}</style></head>
<body><main><h1>SkyOrb firmware update</h1><p>Select the <b>firmware.bin</b> produced by the GitHub build. Do not upload the complete ZIP or an ESP Web Tools manifest.</p>
<p>The display may pause during upload. Keep power connected. A successful upload reboots the ESP32.</p>
<form method="post" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin" required><button type="submit">Install firmware</button></form>
<p><a href="/">Back to SkyOrb dashboard</a></p></main></body></html>
)OTAPAGE";
void onSetAdminPassword(){
  String fresh=server.arg("newpass");String current=server.arg("current");
  if(fresh.length()<8||fresh.length()>48){errorJson("Choose an update password of 8 to 48 characters.");return;}
  Config c=copyCfg();
  if(c.otaPassword.length() && current!=c.otaPassword){errorJson("Current update password is incorrect.",403);return;}
  // Initial setup is only allowed via the recovery/setup AP; once set, the
  // existing password is required even if connected from the setup network.
  if(!c.otaPassword.length() && (!apMode||server.client().localIP()!=WiFi.softAPIP())){
    errorJson("Set the first update password while connected to SKYORB-Setup at 192.168.4.1.",403);return;
  }
  xSemaphoreTake(guard,portMAX_DELAY);cfg.otaPassword=fresh;xSemaphoreGive(guard);savePrefs();
  JsonDocument d;d["ok"]=true;d["message"]="Update password saved. Use username admin when opening the Update page.";replyJson(d);
}
void handleOTAUpload(){
  if(!otaAuthorized())return;
  HTTPUpload &upload=server.upload();
  if(upload.status==UPLOAD_FILE_START){
    otaUploadOK=false;otaError="";
    if(!upload.filename.endsWith(".bin")){otaError="Only firmware.bin files are allowed";return;}
    if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){otaError="Could not start firmware update";Update.printError(Serial);}
  }else if(upload.status==UPLOAD_FILE_WRITE){
    if(otaError.length())return;
    if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize){otaError="Firmware write failed";Update.printError(Serial);}
  }else if(upload.status==UPLOAD_FILE_END){
    if(!otaError.length() && Update.end(true)){otaUploadOK=true;}
    else{if(!otaError.length())otaError="Firmware verification failed";Update.printError(Serial);}
  }else if(upload.status==UPLOAD_FILE_ABORTED){otaError="Upload cancelled";Update.abort();}
}
void finishOTA(){
  if(!otaAuthorized()){requestOtaAuth();return;}
  if(!otaUploadOK){server.send(400,"text/plain",otaError.length()?otaError:"Firmware upload failed");return;}
  server.send(200,"text/html","<h2>SkyOrb firmware uploaded successfully</h2><p>Device is restarting. Please reconnect in about 30 seconds.</p>");
  pendingRestart=true;restartAfter=millis()+1200;
}

void initWeb(){
  server.on("/",HTTP_GET,[](){if(apMode && server.client().localIP()==WiFi.softAPIP() && !copyCfg().ssid.length()){server.sendHeader("Location","/setup",true);server.send(302,"text/plain","");}else server.send_P(200,"text/html",PAGE_HTML);});
  server.on("/setup",HTTP_GET,showSetup);
  server.on("/wifi/scan",HTTP_GET,scanWifi);
  server.on("/admin/password",HTTP_POST,onSetAdminPassword);
  server.on("/update",HTTP_GET,[](){if(!otaAuthorized()){requestOtaAuth();return;}server.send_P(200,"text/html",OTA_HTML);});
  server.on("/update",HTTP_POST,finishOTA,handleOTAUpload);
  server.on("/generate_204",HTTP_GET,captiveRedirect);
  server.on("/hotspot-detect.html",HTTP_GET,captiveRedirect);
  server.on("/connecttest.txt",HTTP_GET,captiveRedirect);
  server.on("/fwlink",HTTP_GET,captiveRedirect);
  server.on("/library/test/success.html",HTTP_GET,captiveRedirect);
  server.on("/api/config",HTTP_GET,publishConfig);
  server.on("/api/live",HTTP_GET,publishLive);
  server.on("/api/geocode",HTTP_GET,onGeocode);
  server.on("/save",HTTP_POST,onSave);
  server.on("/wifi",HTTP_POST,onWifi);
  server.on("/wifi/reset",HTTP_POST,onWifiReset);
  server.on("/airport/add",HTTP_POST,onAirportAdd);
  server.on("/airport/code",HTTP_POST,onAirportCode);
  server.on("/airport/delete",HTTP_POST,onAirportDelete);
  server.on("/airport/clear",HTTP_POST,[](){
    xSemaphoreTake(guard,portMAX_DELAY);cfg.airportCount=0;xSemaphoreGive(guard);
    savePrefs();radarSceneDirty=true;JsonDocument d;d["ok"]=true;replyJson(d);
  });
  server.on("/theme",HTTP_POST,[](){
    String a=server.arg("accent");
    if(a.length()!=7||a[0]!='#'){errorJson("Invalid color");return;}
    for(size_t i=1;i<a.length();i++)if(!isxdigit((unsigned char)a[i])){errorJson("Invalid color");return;}
    xSemaphoreTake(guard,portMAX_DELAY);cfg.accent=a;xSemaphoreGive(guard);
    savePrefs();JsonDocument d;d["ok"]=true;replyJson(d);
  });
  server.on("/airport/auto",HTTP_POST,[](){
    if(!copyCfg().locationSet){errorJson("Set a location first");return;}
    airportProgress(true,"Airport search queued");askAirports=true;JsonDocument d;d["message"]="Airport discovery queued";replyJson(d);
  });
  server.onNotFound([](){if(apMode && server.client().localIP()==WiFi.softAPIP())captiveRedirect();else server.send(404,"text/plain","Not found");});
  server.begin();
}

uint16_t accent565(const String &h){
  if(h.length()!=7||h[0]!='#')return 0x05f5;
  uint32_t c=strtoul(h.c_str()+1,nullptr,16);
  return ((c>>19)&0x1F)<<11|((c>>10)&0x3F)<<5|((c>>3)&0x1F);
}
void center(Adafruit_GFX &t,const String &s,int y,uint8_t sz,uint16_t col){
  t.setTextSize(sz);t.setTextColor(col);t.setCursor(max(0,(240-(int)s.length()*6*sz)/2),y);t.print(s);
}
// Faithful dual-display appearance: crisp white time and amber date on black.
// A complete framebuffer transfer keeps drawing changes invisible to the eye.
// The clock is updated only when text actually changes (once per minute,
// or on date/time-zone/format changes). It NEVER shares the radar background
// buffer and never repeatedly blanks the physical screen.
void renderClock(const Config &c){
  Adafruit_GC9A01A &t=clockScreen(c);
  static Adafruit_GC9A01A *lastDisplay=nullptr;
  static String lastTime="",lastDate="";
  bool first=lastDisplay!=&t;
  if(first){t.fillScreen(0x0000);lastDisplay=&t;lastTime="";lastDate="";}
  time_t utc=time(nullptr);
  if(utc<1700000000){
    if(first){center(t,"SYNCING TIME",105,2,0xFFFF);}
    return;
  }
  time_t localEpoch=utc+c.utcOffset;struct tm tmTime;gmtime_r(&localEpoch,&tmTime);
  int hour=tmTime.tm_hour;bool pm=hour>=12;
  if(!c.clock24){hour%=12;if(hour==0)hour=12;}
  char buf[64];snprintf(buf,sizeof(buf),"%02d:%02d",hour,tmTime.tm_min);
  String top=String(buf)+" "+c.tzAbbr;
  if(top.length()>14)top=top.substring(0,14);
  String stamp=top+(c.clock24?"":pm?" PM":" AM");
  if(stamp!=lastTime){
    t.fillRect(8,77,224,63,0x0000);
    center(t,top,89,top.length()>11?2:3,0xFFFF);
    if(!c.clock24)center(t,pm?"PM":"AM",122,2,0xFFFF);
    lastTime=stamp;
  }
  static const char* weekdays[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
  static const char* months[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
  snprintf(buf,sizeof(buf),"%s %02d, %04d",months[tmTime.tm_mon],tmTime.tm_mday,tmTime.tm_year+1900);
  String date=String(weekdays[tmTime.tm_wday])+"/"+buf;
  if(date!=lastDate){
    t.fillRect(8,139,224,66,0x0000);
    center(t,weekdays[tmTime.tm_wday],142,2,0xFDC7);
    center(t,buf,168,2,0xFDC7);
    lastDate=date;
  }
}
struct XY {int x=120,y=120;float d=0;};
XY project(float latitude,float longitude,const Config &c){
  const float xnm=(longitude-c.lon)*60.0f*cosf(c.lat*DEG2RAD);
  const float ynm=(latitude-c.lat)*60.0f;
  float maxnm=c.radius*MILES_TO_NM;
  XY a;a.x=lroundf(120+100*xnm/maxnm);a.y=lroundf(120-100*ynm/maxnm);
  a.d=sqrtf(xnm*xnm+ynm*ynm);return a;
}
void drawPlane(Adafruit_GFX &t,int x,int y,float heading,uint16_t col){
  float r=heading*DEG2RAD,dx=sinf(r),dy=-cosf(r);
  int noseX=lroundf(x+7*dx),noseY=lroundf(y+7*dy);
  int sideX=lroundf(x-5*dx-4*dy),sideY=lroundf(y-5*dy+4*dx);
  int side2X=lroundf(x-5*dx+4*dy),side2Y=lroundf(y-5*dy-4*dx);
  t.fillTriangle(noseX,noseY,sideX,sideY,side2X,side2Y,col);
}
void drawRadarBackground(const Config &c,Adafruit_GFX &t,bool clearDisplay=true){
  if(clearDisplay)t.fillScreen(0x0000);
  uint16_t accent=green;
  t.drawCircle(120,120,113,accent);
  t.drawCircle(120,120,100,lowgreen);t.drawCircle(120,120,66,lowgreen);t.drawCircle(120,120,33,lowgreen);
  t.drawFastVLine(120,12,216,0x12e4);t.drawFastHLine(12,120,216,0x12e4);
  t.setTextColor(accent);t.setTextSize(1);
  t.setCursor(117,5);t.print("N");t.setCursor(117,230);t.print("S");
  t.setCursor(4,117);t.print("W");t.setCursor(229,117);t.print("E");
  for(int i=0;i<c.airportCount;i++){
    XY p=project(c.airports[i].lat,c.airports[i].lon,c);
    if(p.d>c.radius*MILES_TO_NM||p.x<5||p.x>235||p.y<5||p.y>235)continue;
    t.drawCircle(p.x,p.y,3,0x2DAF);
    if(p.x<194&&p.y>20&&p.y<212){t.setCursor(p.x+5,p.y-7);t.setTextColor(0x2DAF);t.print(c.airports[i].name.substring(0,4));}
  }
  int count=0;String status,source;uint32_t updated=0;Aircraft seen[MAX_AIRCRAFT];
  xSemaphoreTake(guard,portMAX_DELAY);count=live.count;status=live.status;source=live.source;updated=live.updatedAt;
  for(int i=0;i<count;i++)seen[i]=live.flights[i];
  xSemaphoreGive(guard);
  if(updated && millis()-updated>150000UL)count=0;  // never show stale aircraft
  for(int i=0;i<count;i++){
    Aircraft &a=seen[i];XY p=project(a.lat,a.lon,c);
    if(p.d>c.radius*MILES_TO_NM||p.x<8||p.x>231||p.y<8||p.y>231)continue;
    uint16_t col=0x87ff; // blue-white planes on static radar scene
    drawPlane(t,p.x,p.y,a.heading,col);
    String label=a.flight.length()?a.flight:a.hex;
    if(label.length()>8)label.remove(8);
    if(p.x<171 && p.y>20 && p.y<218 && label.length()){
      t.setCursor(p.x+8,p.y-11);t.setTextColor(col);t.print(label);
      if(a.altitude>0){t.setCursor(p.x+8,p.y+1);t.print("FL"+String((a.altitude+50)/100));}
    }
  }
  t.fillRoundRect(80,207,80,17,4,0x0000);
  t.setTextSize(1);t.setTextColor(accent);
  t.setCursor(105,213);t.printf("%d mi",c.radius);
  if(!c.locationSet){
    center(t,"SET LOCATION",119,2,0xffff);
    center(t,WiFi.status()==WL_CONNECTED?"SAVE LOCATION":"CONNECT WIFI",87,1,accent);
    if(apMode)center(t,"SETUP:192.168.4.1",100,1,0xffff);
  } else if(WiFi.status()!=WL_CONNECTED){
    center(t,"WIFI NOT CONNECTED",100,1,0xffff);
  } else if(source=="none")center(t,"FEED UNAVAILABLE",95,1,0xfbc0);
  // mDNS (.local) is unavailable on some phones and routers.  Always show the
  // exact IPv4 address for accessing the settings portal instead.
  if(WiFi.status()==WL_CONNECTED && (millis()<30000UL || !c.locationSet)){
    t.fillRoundRect(42,188,156,13,3,0x0102);
    center(t,WiFi.localIP().toString(),191,1,0xffff);
  }
}

// Direct incremental sweep: no large framebuffer and no full-screen clear
// during animation. Old rays are erased, permanent radar features are restored,
// and new rays are drawn. This keeps aircraft visible even on low-memory boards.
static constexpr int SWEEP_RAYS=10;
static constexpr int SWEEP_LENGTH=100;
static float previousSweep=0.0f;
static bool previousSweepValid=false;
static Adafruit_GC9A01A *lastRadarDisplay=nullptr;
static uint32_t lastRadarScene=0;
static bool lastWifiState=false;
static bool lastApState=false;

void drawMovingSweep(Adafruit_GC9A01A &display,float sweep,bool erase=false){
  for(int k=SWEEP_RAYS-1;k>=0;k--){
    float angle=sweep-k*0.022f;
    uint16_t shade=erase?0x0000:(k==0?0x07E0:(k<3?0x05C0:(k<7?0x0300:0x0180)));
    display.drawLine(120,120,lroundf(120+SWEEP_LENGTH*sinf(angle)),
                     lroundf(120-SWEEP_LENGTH*cosf(angle)),shade);
  }
}
void renderRadar(const Config &c){
  Adafruit_GC9A01A &display=radarScreen(c);
  uint32_t now=millis();
  bool connected=WiFi.status()==WL_CONNECTED;
  bool sceneRefresh=radarSceneDirty || lastRadarDisplay!=&display ||
    (now-lastRadarScene>30000UL) || connected!=lastWifiState || apMode!=lastApState;
  if(sceneRefresh){
    radarSceneDirty=false;
    lastRadarDisplay=&display;
    lastWifiState=connected;
    lastApState=apMode;
    lastRadarScene=now;
    drawRadarBackground(c,display,true);
    previousSweepValid=false;
  }
  float sweep=fmodf((now%11000UL)*360.0f/11000.0f,360.0f)*DEG2RAD;
  if(previousSweepValid){
    // Remove only the previously drawn sweep, then restore background features
    // crossing that narrow wedge. Never clear the entire display here.
    drawMovingSweep(display,previousSweep,true);
    drawRadarBackground(c,display,false);
  }
  drawMovingSweep(display,sweep,false);
  previousSweep=sweep;
  previousSweepValid=true;
}

void renderBoot(){
  radarPhysical.fillScreen(0);clockPhysical.fillScreen(0);
  center(radarPhysical,"SkyOrb Classic",91,2,green);
  center(clockPhysical,"SkyOrb Classic",91,2,green);
  center(radarPhysical,"Booting...",119,1,0xffff);
  center(clockPhysical,"Booting...",119,1,0xffff);
}

void setup(){
  Serial.begin(115200);delay(250);
  guard=xSemaphoreCreateMutex();loadPrefs();
  Serial.printf("[Graphics] Direct sweep (no full-screen buffer), free heap: %u\n",ESP.getFreeHeap());
  SPI.begin(18,19,23,-1); // Shared VSPI: SCK=18, MISO=19 (unused), MOSI=23.
  // Guard each chip select against both displays selecting the bus during boot.
  pinMode(PIN_RADAR_CS,OUTPUT);pinMode(PIN_CLOCK_CS,OUTPUT);
  digitalWrite(PIN_RADAR_CS,HIGH);digitalWrite(PIN_CLOCK_CS,HIGH);
  radarPhysical.begin(27000000);clockPhysical.begin(27000000);
  radarPhysical.setRotation(0);clockPhysical.setRotation(0);
  renderBoot();
  // Always provide 192.168.4.1 during the setup window; no more lockout
  // between entering Wi-Fi details and setting an address.
  startSetupAP();
  startWifiConnection();
  initWeb();
  xTaskCreatePinnedToCore(worker,"flightfetch",16384,nullptr,1,nullptr,0);
  askTZ=copyCfg().locationSet;
  askFlights=true;
  Serial.println("[Web] http://skyorb.local or ESP32 IP / [Debug] Serial 115200");
}
void loop(){
  server.handleClient();if(apMode)dns.processNextRequest();
  if(shouldSave)savePrefs();
  if(pendingRestart && (int32_t)(millis()-restartAfter)>0)ESP.restart();
  static uint32_t radarFrame=0,clockFrame=0, wifiCheck=0, disconnectedAt=0;
  uint32_t now=millis();
  if(now-radarFrame>100){radarFrame=now;Config c=copyCfg();renderRadar(c);}
  if(now-clockFrame>1000){clockFrame=now;Config c=copyCfg();renderClock(c);}
  if(pendingWifiConnect){
    pendingWifiConnect=false;
    WiFi.disconnect(false,false);
    startWifiConnection();
  }
  if(now-wifiCheck>5000){
    wifiCheck=now;
    const bool connected=WiFi.status()==WL_CONNECTED;
    if(connected && !servicesReady){
      servicesReady=true;
      MDNS.begin("skyorb");
      configTime(0,0,"pool.ntp.org","time.google.com");
      Serial.printf("[WiFi] Connected. Home IP: %s\n",WiFi.localIP().toString().c_str());
    }
    if(!connected && servicesReady){
      MDNS.end();servicesReady=false;
    }
    if(!connected){
      if(!disconnectedAt)disconnectedAt=now;
      if(now-disconnectedAt>15000UL){WiFi.reconnect();disconnectedAt=now;}
      if(!apMode)startSetupAP();
    }else{
      disconnectedAt=0;
      // After the radar is configured, close the well-known-password hotspot.
      // Restarting the device opens a new 15-minute recovery window.
      if(apMode && copyCfg().locationSet && now>AP_GRACE_PERIOD){
        dns.stop();WiFi.softAPdisconnect(true);apMode=false;
        WiFi.mode(WIFI_STA);
        Serial.println("[WiFi] Recovery hotspot closed. Power-cycle to re-open it.");
      }
    }
  }
  delay(2);
}

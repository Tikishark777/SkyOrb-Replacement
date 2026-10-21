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
#include <time.h>
#include <math.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
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
WebServer server(80);
DNSServer dns;
Preferences prefs;
SemaphoreHandle_t guard;
bool apMode=false;
volatile bool askTZ=false, askAirports=false, askFlights=true, shouldSave=false;

struct Airport {String name; float lat=0, lon=0;};
struct Aircraft {
  String flight, hex, reg;
  float lat=0, lon=0, heading=0;
  int altitude=0;
  bool ground=false;
};
struct Config {
  String ssid, password, tzName="UTC", accent="#32d9bc";
  float lat=0, lon=0;
  int radius=30;
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

static uint16_t green=0x05f5, lowgreen=0x01c5, pale=0xd7ff, amber=0xf5e8;
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
  prefs.putString("tz",c.tzName);prefs.putInt("offset",c.utcOffset);
  JsonDocument d;JsonArray a=d.to<JsonArray>();
  for(int i=0;i<c.airportCount;i++){JsonObject o=a.add<JsonObject>();o["name"]=c.airports[i].name;o["lat"]=c.airports[i].lat;o["lon"]=c.airports[i].lon;}
  String json;serializeJson(d,json);prefs.putString("airports",json);
  shouldSave=false;
}
void loadPrefs(){
  prefs.begin("skyorb-v2",false);
  cfg.ssid=prefs.getString("ssid","");cfg.password=prefs.getString("pass","");
  cfg.locationSet=prefs.getBool("hasLoc",false);cfg.lat=prefs.getFloat("lat",0);cfg.lon=prefs.getFloat("lon",0);
  cfg.radius=clampI(prefs.getInt("rad",30),5,30);
  cfg.clock24=prefs.getBool("24h",false);cfg.swapScreens=prefs.getBool("swap",false);
  cfg.fallback=prefs.getFloat("fallback",0);cfg.accent=prefs.getString("accent","#32d9bc");
  cfg.tzName=prefs.getString("tz","UTC");cfg.utcOffset=prefs.getInt("offset",0);
  String airportsJson=prefs.getString("airports","[]");JsonDocument d;
  if(!deserializeJson(d,airportsJson)){
    for(JsonObject a:d.as<JsonArray>()){
      if(cfg.airportCount>=MAX_AIRPORTS)break;
      Airport &v=cfg.airports[cfg.airportCount++];v.name=cleanName(a["name"].as<String>(),8);
      v.lat=a["lat"].as<float>();v.lon=a["lon"].as<float>();
    }
  }
}

bool connectWifi(){
  WiFi.mode(WIFI_STA);WiFi.setSleep(false);
  Config c=copyCfg();if(!c.ssid.length())return false;
  WiFi.begin(c.ssid.c_str(),c.password.c_str());
  uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED && millis()-start<15000){delay(150);}
  return WiFi.status()==WL_CONNECTED;
}
void startSetupAP(){
  apMode=true;WiFi.mode(WIFI_AP_STA);WiFi.softAP("SKYORB-Setup","SKYORB1234");
  dns.start(53,"*",WiFi.softAPIP());
  Serial.printf("[WiFi] Setup AP: http://%s\n",WiFi.softAPIP().toString().c_str());
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

void refreshTZ(){
  Config c=copyCfg();if(!c.locationSet||WiFi.status()!=WL_CONNECTED)return;
  String url="https://api.open-meteo.com/v1/forecast?latitude="+String(c.lat,5)+"&longitude="+String(c.lon,5)+"&current=temperature_2m&timezone=auto&forecast_days=1";
  JsonDocument d;int status=0;
  if(getJsonHTTPS(url,d,status)){
    String name=d["timezone"]|"";int offset=d["utc_offset_seconds"]|0;
    if(name.length()){
      xSemaphoreTake(guard,portMAX_DELAY);cfg.tzName=name;cfg.utcOffset=offset;xSemaphoreGive(guard);
      shouldSave=true;
      Serial.printf("[Clock] %s, offset %+d seconds\n",name.c_str(),offset);return;
    }
  }
  // Manual fallback only if we have never successfully resolved a zone.
  xSemaphoreTake(guard,portMAX_DELAY);
  if(cfg.tzName=="UTC"||cfg.tzName=="UTC fallback"){
    cfg.utcOffset=roundf(cfg.fallback*3600);cfg.tzName="UTC fallback";
  }
  xSemaphoreGive(guard);
}

// Auto-discover coded aerodromes from OSM; this is user-triggered or one time
// after location is saved, not a repeated polling service.
void discoverAirports(){
  Config c=copyCfg();if(!c.locationSet||WiFi.status()!=WL_CONNECTED)return;
  int searchM=(int)(c.radius*1609.344f); // radius in meters
  String query="[out:json][timeout:18];nwr(around:"+String(searchM)+","+String(c.lat,5)+","+String(c.lon,5)+")[\"aeroway\"=\"aerodrome\"][\"iata\"];out center 30;";
  String url="https://overpass-api.de/api/interpreter?data="+urlEncode(query);
  JsonDocument doc;int code=0;
  if(!getJsonHTTPS(url,doc,code)){
    Serial.printf("[Airports] Overpass failed: %d; existing markers retained\n",code);return;
  }
  Airport found[MAX_AIRPORTS];int n=0;
  JsonArray elements=doc["elements"].as<JsonArray>();
  for(JsonObject e:elements){
    if(n>=MAX_AIRPORTS)break;
    String name=cleanName(e["tags"]["iata"].as<String>(),8);if(!name.length())continue;
    float a=e["lat"].isNull()?e["center"]["lat"].as<float>():e["lat"].as<float>();
    float o=e["lon"].isNull()?e["center"]["lon"].as<float>():e["lon"].as<float>();
    if(a<-90||a>90||o<-180||o>180)continue;
    bool duplicate=false;for(int j=0;j<n;j++)if(found[j].name==name)duplicate=true;
    if(duplicate)continue;
    found[n].name=name;found[n].lat=a;found[n].lon=o;n++;
  }
  if(n){
    xSemaphoreTake(guard,portMAX_DELAY);
    cfg.airportCount=n;for(int i=0;i<n;i++)cfg.airports[i]=found[i];
    xSemaphoreGive(guard);shouldSave=true;
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
  const char* keys[]={"hex","flight","r","lat","lon","alt_baro","track","seen_pos"};
  for(const char *key:keys)filter["ac"][0][key]=true;
  int lastCode=0;
  for(int source=0;source<2;source++){
    JsonDocument doc;int code=0;
    if(!getJsonHTTPS(urls[source],doc,code,&filter)){lastCode=code;continue;}
    JsonArray ac=doc["ac"].as<JsonArray>();
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
      p.heading=a["track"]|0.0f;
      if(a["alt_baro"].is<const char*>()){
        String s=a["alt_baro"].as<String>();p.ground=(s=="ground");p.altitude=s.toInt();
      }else{p.altitude=a["alt_baro"]|0;}
    }
    xSemaphoreTake(guard,portMAX_DELAY);
    live.count=count;for(int i=0;i<count;i++)live.flights[i]=tmp[i];
    live.updatedAt=millis();live.httpCode=code;live.source=providers[source];
    live.status=count?"Tracking aircraft":"Connected; no aircraft currently in range";
    xSemaphoreGive(guard);
    Serial.printf("[Flights] %s: %d nearby aircraft\n",providers[source].c_str(),count);
    return;
  }
  xSemaphoreTake(guard,portMAX_DELAY);
  live.count=0;live.httpCode=lastCode;live.source="none";
  live.status="Flight-data request failed (see Serial / API status)";
  xSemaphoreGive(guard);
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
  d["accent"]=c.accent;d["timezone"]=c.tzName;d["fallback"]=c.fallback;
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
  d["wifi"]=WiFi.status()==WL_CONNECTED?"connected":"setup / disconnected";
  d["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();
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
    a["heading"]=p.heading;a["altitude"]=p.altitude;a["ground"]=p.ground;
  }
  xSemaphoreGive(guard);
  replyJson(d);
}
void onSave(){
  if(!server.hasArg("lat")||!server.hasArg("lon")){errorJson("Latitude and longitude required");return;}
  float lat=server.arg("lat").toFloat(),lon=server.arg("lon").toFloat();
  if(lat<-90||lat>90||lon<-180||lon>180){errorJson("Invalid coordinates");return;}
  String accent=server.arg("accent");
  if(accent.length()!=7||accent[0]!='#')accent="#32d9bc";
  Config prev=copyCfg();bool moved=!prev.locationSet||fabsf(prev.lat-lat)>0.0001f||fabsf(prev.lon-lon)>0.0001f;
  xSemaphoreTake(guard,portMAX_DELAY);
  cfg.lat=lat;cfg.lon=lon;cfg.radius=clampI(server.arg("radius").toInt(),5,30);
  cfg.clock24=server.arg("clock24")=="1";cfg.swapScreens=server.hasArg("swap");
  cfg.accent=accent;cfg.fallback=clampF(server.arg("fallback").toFloat(),-12,14);
  cfg.locationSet=true;
  if(moved){cfg.tzName="UTC fallback";cfg.utcOffset=roundf(cfg.fallback*3600);cfg.airportCount=0;}
  xSemaphoreGive(guard);
  savePrefs();askFlights=true;askTZ=true;if(moved)askAirports=true;
  redirectRoot();
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
  lastAt=millis();
  if(WiFi.status()!=WL_CONNECTED){errorJson("Connect SkyOrb to home Wi-Fi before searching addresses",503);return;}
  String url="https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&q="+urlEncode(q);
  JsonDocument d;int code=0;
  if(!getJsonHTTPS(url,d,code)){errorJson("OpenStreetMap address lookup failed: "+String(code),502);return;}
  JsonArray result=d.as<JsonArray>();if(result.isNull()||result.size()==0){errorJson("No address found; use coordinates instead",404);return;}
  lastLat=result[0]["lat"].as<float>();lastLon=result[0]["lon"].as<float>();lastQuery=q;
  JsonDocument reply;reply["lat"]=lastLat;reply["lon"]=lastLon;replyJson(reply);
}
void onWifi(){
  String ssid=server.arg("ssid");ssid.trim();String pw=server.arg("password");
  if(!ssid.length()||ssid.length()>32||pw.length()>63){errorJson("Invalid Wi-Fi credentials");return;}
  xSemaphoreTake(guard,portMAX_DELAY);cfg.ssid=ssid;cfg.password=pw;xSemaphoreGive(guard);
  savePrefs();server.send(200,"text/html","<p>Wi-Fi saved. SkyOrb is restarting. Reconnect your phone to your home Wi-Fi, then open <a href='http://skyorb.local'>skyorb.local</a>.</p>");
  delay(500);ESP.restart();
}
void onWifiReset(){xSemaphoreTake(guard,portMAX_DELAY);cfg.ssid="";cfg.password="";xSemaphoreGive(guard);savePrefs();server.send(200,"text/plain","Wi-Fi cleared; restarting into setup mode");delay(500);ESP.restart();}
void onAirportAdd(){
  String name=cleanName(server.arg("name"),8);
  float lat=server.arg("lat").toFloat(),lon=server.arg("lon").toFloat();
  if(!name.length()||lat<-90||lat>90||lon<-180||lon>180){errorJson("Bad airport fields");return;}
  xSemaphoreTake(guard,portMAX_DELAY);
  if(cfg.airportCount>=MAX_AIRPORTS){xSemaphoreGive(guard);errorJson("Airport list full");return;}
  Airport &a=cfg.airports[cfg.airportCount++];a.name=name;a.lat=lat;a.lon=lon;
  xSemaphoreGive(guard);savePrefs();redirectRoot();
}
void onAirportDelete(){int i=server.arg("i").toInt();xSemaphoreTake(guard,portMAX_DELAY);
  if(i<0||i>=cfg.airportCount){xSemaphoreGive(guard);errorJson("No such airport");return;}
  for(int j=i;j<cfg.airportCount-1;j++)cfg.airports[j]=cfg.airports[j+1];cfg.airportCount--;
  xSemaphoreGive(guard);savePrefs();JsonDocument d;d["ok"]=true;replyJson(d);
}
void initWeb(){
  server.on("/",HTTP_GET,[](){server.send_P(200,"text/html",PAGE_HTML);});
  server.on("/api/config",HTTP_GET,publishConfig);
  server.on("/api/live",HTTP_GET,publishLive);
  server.on("/api/geocode",HTTP_GET,onGeocode);
  server.on("/save",HTTP_POST,onSave);
  server.on("/wifi",HTTP_POST,onWifi);
  server.on("/wifi/reset",HTTP_POST,onWifiReset);
  server.on("/airport/add",HTTP_POST,onAirportAdd);
  server.on("/airport/delete",HTTP_POST,onAirportDelete);
  server.on("/airport/auto",HTTP_POST,[](){
    if(!copyCfg().locationSet){errorJson("Set a location first");return;}
    askAirports=true;JsonDocument d;d["message"]="Airport discovery queued; reload after 10–20 seconds";replyJson(d);
  });
  server.onNotFound([](){if(apMode){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");});
  server.begin();
}

uint16_t accent565(const String &h){
  if(h.length()!=7||h[0]!='#')return 0x05f5;
  uint32_t c=strtoul(h.c_str()+1,nullptr,16);
  return ((c>>19)&0x1F)<<11|((c>>10)&0x3F)<<5|((c>>3)&0x1F);
}
void center(Adafruit_GC9A01A &t,const String &s,int y,uint8_t sz,uint16_t col){
  t.setTextSize(sz);t.setTextColor(col);t.setCursor(max(0,(240-(int)s.length()*6*sz)/2),y);t.print(s);
}
void renderClock(const Config &c){
  Adafruit_GC9A01A &t=clockScreen(c);t.fillScreen(0x0000);
  uint16_t accent=accent565(c.accent);
  t.drawCircle(120,120,115,accent);t.drawCircle(120,120,110,0x29c5);
  center(t,"SKYORB",37,2,accent);
  time_t utc=time(nullptr);
  if(utc<1700000000){center(t,"SYNCING",105,2,0xffff);center(t,"NTP CLOCK",136,1,0xb596);return;}
  time_t localEpoch=utc+c.utcOffset;struct tm tmTime;gmtime_r(&localEpoch,&tmTime);
  int hour=tmTime.tm_hour;bool pm=hour>=12;
  if(!c.clock24){hour%=12;if(hour==0)hour=12;}
  char buf[64];snprintf(buf,sizeof(buf),"%02d:%02d",hour,tmTime.tm_min);
  center(t,buf,79,4,0xffff);
  snprintf(buf,sizeof(buf),"%02d SECONDS",tmTime.tm_sec);
  center(t,buf,125,1,accent);
  if(!c.clock24)center(t,pm?"PM":"AM",143,2,accent);
  static const char* weekdays[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
  static const char* months[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  snprintf(buf,sizeof(buf),"%s  %s %d",weekdays[tmTime.tm_wday],months[tmTime.tm_mon],tmTime.tm_mday);
  center(t,buf,176,2,0xffff);
  snprintf(buf,sizeof(buf),"%04d",tmTime.tm_year+1900);center(t,buf,198,1,0x9d35);
}
struct XY {int x=120,y=120;float d=0;};
XY project(float latitude,float longitude,const Config &c){
  const float xnm=(longitude-c.lon)*60.0f*cosf(c.lat*DEG2RAD);
  const float ynm=(latitude-c.lat)*60.0f;
  float maxnm=c.radius*MILES_TO_NM;
  XY a;a.x=lroundf(120+100*xnm/maxnm);a.y=lroundf(120-100*ynm/maxnm);
  a.d=sqrtf(xnm*xnm+ynm*ynm);return a;
}
void drawPlane(Adafruit_GC9A01A &t,int x,int y,float heading,uint16_t col){
  float r=heading*DEG2RAD,dx=sinf(r),dy=-cosf(r);
  int noseX=lroundf(x+7*dx),noseY=lroundf(y+7*dy);
  int sideX=lroundf(x-5*dx-4*dy),sideY=lroundf(y-5*dy+4*dx);
  int side2X=lroundf(x-5*dx+4*dy),side2Y=lroundf(y-5*dy-4*dx);
  t.fillTriangle(noseX,noseY,sideX,sideY,side2X,side2Y,col);
}
void renderRadar(const Config &c){
  Adafruit_GC9A01A &t=radarScreen(c);
  t.fillScreen(0x0000);
  uint16_t accent=accent565(c.accent);
  t.drawCircle(120,120,113,accent);
  t.drawCircle(120,120,100,lowgreen);t.drawCircle(120,120,66,lowgreen);t.drawCircle(120,120,33,lowgreen);
  t.drawFastVLine(120,12,216,0x12e4);t.drawFastHLine(12,120,216,0x12e4);
  t.setTextColor(accent);t.setTextSize(1);
  t.setCursor(117,5);t.print("N");t.setCursor(117,230);t.print("S");
  t.setCursor(4,117);t.print("W");t.setCursor(229,117);t.print("E");
  float sweep=fmodf((millis()%11000UL)*360.0f/11000.0f,360.0f)*DEG2RAD;
  for(int k=4;k>=0;k--){
    float angle=sweep-k*0.055f;
    uint16_t shade=(k==0?accent:(k<3?0x03a8:0x01c4));
    t.drawLine(120,120,lroundf(120+100*sinf(angle)),lroundf(120-100*cosf(angle)),shade);
  }
  for(int i=0;i<c.airportCount;i++){
    XY p=project(c.airports[i].lat,c.airports[i].lon,c);
    if(p.d>c.radius*MILES_TO_NM||p.x<5||p.x>235||p.y<5||p.y>235)continue;
    t.fillRect(p.x-2,p.y-2,5,5,amber);
    if(p.x<194&&p.y>20&&p.y<212){t.setCursor(p.x+5,p.y-7);t.setTextColor(amber);t.print(c.airports[i].name.substring(0,4));}
  }
  int count=0;String status,source;uint32_t updated=0;Aircraft seen[MAX_AIRCRAFT];
  xSemaphoreTake(guard,portMAX_DELAY);count=live.count;status=live.status;source=live.source;updated=live.updatedAt;
  for(int i=0;i<count;i++)seen[i]=live.flights[i];
  xSemaphoreGive(guard);
  if(updated && millis()-updated>150000UL)count=0;  // never show stale aircraft
  for(int i=0;i<count;i++){
    Aircraft &a=seen[i];XY p=project(a.lat,a.lon,c);
    if(p.d>c.radius*MILES_TO_NM||p.x<8||p.x>231||p.y<8||p.y>231)continue;
    float bearing=atan2f((a.lon-c.lon)*cosf(c.lat*DEG2RAD),(a.lat-c.lat));
    if(bearing<0)bearing+=2*PI;
    float difference=fabsf(atan2f(sinf(bearing-sweep),cosf(bearing-sweep)));
    uint16_t col=difference<0.28f?0xffc0:accent;
    if(difference<0.20f)t.drawCircle(p.x,p.y,9,col);
    drawPlane(t,p.x,p.y,a.heading,col);
    String label=a.flight.length()?a.flight:a.hex;
    if(label.length()>8)label.remove(8);
    if(p.x<171 && p.y>20 && p.y<218 && label.length()){
      t.setCursor(p.x+8,p.y-11);t.setTextColor(col);t.print(label);
      if(a.altitude>0){t.setCursor(p.x+8,p.y+1);t.print(String(a.altitude));}
    }
  }
  t.fillRoundRect(67,207,106,20,6,0x0102);
  t.setTextSize(1);t.setTextColor(accent);
  t.setCursor(73,213);t.printf("%d AC  %d MI",count,c.radius);
  if(!c.locationSet){center(t,"SET LOCATION",103,2,0xffff);}
  if(WiFi.status()!=WL_CONNECTED){
    center(t,"SKYORB-Setup",84,1,0xffff);center(t,"192.168.4.1",96,1,0xffff);
  }else if(!c.locationSet){center(t,"skyorb.local",90,1,0xffff);}
  else if(source=="none")center(t,"FEED UNAVAILABLE",95,1,0xfbc0);
}
void renderBoot(){
  radarPhysical.fillScreen(0);clockPhysical.fillScreen(0);
  center(radarPhysical,"SkyOrb 2",91,2,green);
  center(clockPhysical,"SkyOrb 2",91,2,green);
  center(radarPhysical,"Booting...",119,1,0xffff);
  center(clockPhysical,"Booting...",119,1,0xffff);
}

void setup(){
  Serial.begin(115200);delay(250);
  guard=xSemaphoreCreateMutex();loadPrefs();
  SPI.begin(18,19,23,-1); // Shared VSPI: SCK=18, MISO=19 (unused), MOSI=23.
  // Guard each chip select against both displays selecting the bus during boot.
  pinMode(PIN_RADAR_CS,OUTPUT);pinMode(PIN_CLOCK_CS,OUTPUT);
  digitalWrite(PIN_RADAR_CS,HIGH);digitalWrite(PIN_CLOCK_CS,HIGH);
  radarPhysical.begin(27000000);clockPhysical.begin(27000000);
  radarPhysical.setRotation(0);clockPhysical.setRotation(0);
  renderBoot();
  if(!connectWifi()){startSetupAP();}
  else{
    Serial.printf("[WiFi] Connected: %s\n",WiFi.localIP().toString().c_str());
    MDNS.begin("skyorb");
    configTime(0,0,"pool.ntp.org","time.google.com");
  }
  initWeb();
  xTaskCreatePinnedToCore(worker,"flightfetch",16384,nullptr,1,nullptr,0);
  askTZ=copyCfg().locationSet;
  askFlights=true;
  Serial.println("[Web] http://skyorb.local or ESP32 IP / [Debug] Serial 115200");
}
void loop(){
  server.handleClient();if(apMode)dns.processNextRequest();
  if(shouldSave)savePrefs();
  static uint32_t radarFrame=0,clockFrame=0, wifiCheck=0;
  uint32_t now=millis();
  if(now-radarFrame>140){radarFrame=now;Config c=copyCfg();renderRadar(c);}
  if(now-clockFrame>1000){clockFrame=now;Config c=copyCfg();renderClock(c);}
  if(now-wifiCheck>20000){
    wifiCheck=now;
    if(!apMode && WiFi.status()!=WL_CONNECTED){WiFi.reconnect();}
  }
  delay(2);
}

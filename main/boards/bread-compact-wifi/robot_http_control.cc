#include "robot_http_control.h"
#include "motor_driver.h"
#include "application.h"
#include "device_state.h"
#include "board.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_mac.h>

static const char* TAG = "RobotHTTP";

static std::atomic<int>  g_mode{0};
static httpd_handle_t    g_server = nullptr;

// ---------------------------------------------------------------------------
// Embedded HTML
// PLAY mode  → WebSocket (ws://robot-ip/ws) – zero HTTP overhead, < 5 ms
// VOICE mode → normal Xiaozhi conversation cycle
// ---------------------------------------------------------------------------
static const char kIndexHtml[] =
    "<!DOCTYPE html><html lang='fr'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
    "<title>Robot Control</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}"
    "html,body{height:100%;overflow:hidden}"
    "body{background:#0d0d1a;color:#e0e0ff;font-family:system-ui,sans-serif;"
    "display:flex;flex-direction:column;align-items:center;"
    "padding:env(safe-area-inset-top,10px) 12px env(safe-area-inset-bottom,10px);"
    "gap:10px;touch-action:none;user-select:none;-webkit-user-select:none}"
    "h1{font-size:clamp(1rem,4vw,1.35rem);letter-spacing:.06em;flex-shrink:0;"
    "background:linear-gradient(90deg,#7c5cfc,#00d4ff);-webkit-background-clip:text;"
    "-webkit-text-fill-color:transparent}"
    "#status{font-size:clamp(.6rem,2.5vw,.72rem);opacity:.38;flex-shrink:0}"
    "#ws-badge{display:inline-block;margin-left:6px;padding:1px 6px;border-radius:8px;"
    "font-size:.6rem;background:rgba(80,80,110,.5);color:#888;vertical-align:middle}"
    "#ws-badge.ok{background:rgba(0,212,80,.25);color:#00d450}"
    "#ws-badge.err{background:rgba(252,80,80,.25);color:#fc5050}"
    ".mode-bar{display:flex;border-radius:12px;overflow:hidden;"
    "border:1px solid rgba(124,92,252,.28);width:100%;max-width:340px;flex-shrink:0}"
    ".mb{flex:1;padding:clamp(7px,2.5vw,11px) 2px;font-size:clamp(.7rem,3vw,.8rem);"
    "text-align:center;cursor:pointer;"
    "background:rgba(255,255,255,.04);color:rgba(224,224,255,.45);border:none;outline:none}"
    ".mb.on-play{background:rgba(124,92,252,.42);color:#fff;font-weight:700}"
    ".mb.on-talk{background:rgba(0,212,255,.28);color:#00d4ff;font-weight:700}"
    ".mb.on-off {background:rgba(80,80,110,.38);color:#999;font-weight:700}"
    ".sliders{width:100%;max-width:340px;display:flex;flex-direction:column;gap:6px;flex-shrink:0}"
    ".sr{display:flex;align-items:center;gap:8px}"
    ".sl{font-size:clamp(.62rem,2.5vw,.74rem);opacity:.45;min-width:48px}"
    "input[type=range]{flex:1;accent-color:#7c5cfc;height:4px;cursor:pointer}"
    ".sv{min-width:2.8em;text-align:right;font-size:clamp(.72rem,3vw,.86rem);color:#a07cff}"
    ".joys{display:flex;flex-direction:column;align-items:center;gap:8px;flex:1;justify-content:center}"
    ".jw{display:flex;flex-direction:column;align-items:center;gap:3px}"
    ".jl{font-size:clamp(.58rem,2vw,.66rem);opacity:.38;letter-spacing:.06em;text-transform:uppercase}"
    ".jc{position:relative;"
    "width:clamp(120px,38vw,155px);height:clamp(120px,38vw,155px);"
    "border-radius:50%;background:rgba(255,255,255,.04);"
    "border:2px solid rgba(124,92,252,.26);touch-action:none}"
    ".st{position:absolute;border-radius:50%;"
    "width:clamp(44px,14vw,54px);height:clamp(44px,14vw,54px);"
    "top:50%;left:50%;transform:translate(-50%,-50%);"
    "background:radial-gradient(circle at 33% 33%,#b898ff,#7c5cfc);"
    "box-shadow:0 0 16px rgba(124,92,252,.5)}"
    "#talk-panel{display:none;flex-direction:column;align-items:center;gap:12px;flex:1;justify-content:center}"
    ".mic-ring{width:clamp(80px,22vw,110px);height:clamp(80px,22vw,110px);"
    "border-radius:50%;border:3px solid rgba(0,212,255,.5);"
    "display:flex;align-items:center;justify-content:center;"
    "font-size:clamp(2rem,8vw,3rem);animation:pulse 1.8s infinite}"
    "@keyframes pulse{0%,100%{box-shadow:0 0 0 0 rgba(0,212,255,.4)}"
    "50%{box-shadow:0 0 0 16px rgba(0,212,255,0)}}"
    ".talk-hint{font-size:clamp(.68rem,2.8vw,.78rem);opacity:.45;text-align:center;"
    "max-width:260px;line-height:1.6}"
    ".langs{display:flex;gap:8px;margin-top:4px}"
    ".lb{padding:7px 14px;border-radius:20px;font-size:clamp(.68rem,2.8vw,.78rem);"
    "border:1px solid rgba(0,212,255,.3);background:rgba(0,212,255,.07);"
    "color:rgba(224,224,255,.55);cursor:pointer}"
    ".lb.sel{background:rgba(0,212,255,.3);color:#00d4ff;border-color:rgba(0,212,255,.6);font-weight:700}"
    "#off-panel{display:none;flex-direction:column;align-items:center;gap:8px;flex:1;justify-content:center}"
    ".off-icon{font-size:clamp(2.5rem,10vw,3.5rem);opacity:.3}"
    ".off-hint{font-size:clamp(.64rem,2.5vw,.76rem);opacity:.3}"
    "</style></head><body>"
    "<h1>&#x1F916;&nbsp;Robot Control"
    "<span id='ws-badge'>WS&nbsp;&#x25CF;</span></h1>"
    "<div id='status'>connexion...</div>"
    "<div class='mode-bar'>"
    "<button class='mb on-play' id='m0' onclick='setMode(0)'>&#x1F3AE;&nbsp;PLAY</button>"
    "<button class='mb' id='m1' onclick='setMode(1)'>&#x1F3A4;&nbsp;PARLER</button>"
    "<button class='mb' id='m2' onclick='setMode(2)'>&#x1F4A4;&nbsp;OFF</button>"
    "</div>"
    "<div class='sliders'>"
    "<div class='sr'>"
    "<span class='sl'>&#x26A1;&nbsp;Vitesse</span>"
    "<input type='range' id='spd' min='25' max='80' value='50' step='5'"
    " oninput='document.getElementById(\"sv\").textContent=this.value+\"%\"'>"
    "<span class='sv' id='sv'>50%</span>"
    "</div>"
    "<div class='sr'>"
    "<span class='sl'>&#x1F50A;&nbsp;Volume</span>"
    "<input type='range' id='vol' min='0' max='100' value='70' step='5'"
    " oninput='onVol(this.value)'>"
    "<span class='sv' id='vv'>70%</span>"
    "</div>"
    "</div>"
    "<div class='joys' id='play-panel'>"
    "<div class='jw'><span class='jl'>Avance / Recul</span>"
    "<div class='jc' id='jD'><div class='st' id='sD'></div></div></div>"
    "<div class='jw'><span class='jl'>Direction</span>"
    "<div class='jc' id='jS'><div class='st' id='sS'></div></div></div>"
    "</div>"
    "<div id='talk-panel'>"
    "<div class='mic-ring'>&#x1F3A4;</div>"
    "<div class='talk-hint'>Parlez normalement &mdash; le robot &#xe9;coute et r&#xe9;pond.<br>"
    "Ex&nbsp;: &laquo;&nbsp;avance de 10&nbsp;cm&nbsp;&raquo;</div>"
    "<div class='langs'>"
    "<button class='lb' id='lFR' onclick='setLang(\"fr\")'>&#x1F1EB;&#x1F1F7;&nbsp;FR</button>"
    "<button class='lb' id='lEN' onclick='setLang(\"en\")'>&#x1F1EC;&#x1F1E7;&nbsp;EN</button>"
    "<button class='lb' id='lAR' onclick='setLang(\"ar\")'>&#x1F1E9;&#x1F1FF;&nbsp;AR</button>"
    "</div>"
    "</div>"
    "<div id='off-panel'>"
    "<div class='off-icon'>&#x1F4A4;</div>"
    "<div class='off-hint'>Mode veille &mdash; appuyez PLAY pour reprendre</div>"
    "</div>"

    "<script>"
    /* ── WebSocket (PLAY mode motor commands – zero HTTP overhead) ── */
    "var ws=null,wsOk=false,wsDelay=1500;"
    "var badge=document.getElementById('ws-badge');"
    "function wsBadge(s){"
    "badge.className='ws-badge'+(s==='ok'?' ok':s==='err'?' err':'');"
    "badge.textContent=s==='ok'?'WS \\u25cf':s==='err'?'WS \\u2715':'WS \\u25cb';}"
    "function wsConnect(){"
    "if(ws)return;"
    "try{"
    "ws=new WebSocket('ws://'+location.host+'/ws');"
    "ws.onopen=function(){wsOk=true;wsDelay=1500;wsBadge('ok');"
    "document.getElementById('status').textContent='pr\\u00eat \\u00b7 WebSocket';};"
    "ws.onclose=function(){wsOk=false;ws=null;wsBadge('err');"
    "setTimeout(wsConnect,wsDelay);"
    "wsDelay=Math.min(wsDelay*2,10000);};"
    "ws.onerror=function(e){};"
    "}catch(err){"
    "wsBadge('err');"
    "document.getElementById('status').textContent='WS err: '+err.message;"
    "setTimeout(wsConnect,5000);}}"
    "wsConnect();"
    "function wsSend(msg){if(wsOk&&ws&&ws.readyState===1)ws.send(msg);}"

    /* ── mode switch ── */
    "var mode=0;"
    "var MODES=['on-play','on-talk','on-off'];"
    "var PANELS=['play-panel','talk-panel','off-panel'];"
    "function setMode(m){"
    "if(m===mode)return;mode=m;"
    "for(var i=0;i<3;i++){"
    "document.getElementById('m'+i).className='mb'+(i===m?' '+MODES[i]:'');"
    "document.getElementById(PANELS[i]).style.display=i===m?'flex':'none';}"
    "if(m!==0){jD._dy=0;jS._dx=0;"
    "document.getElementById('sD').style.transform='translate(-50%,-50%)';"
    "document.getElementById('sS').style.transform='translate(-50%,-50%)';"
    "wsSend('stop');}"
    /* Notify server of mode change via HTTP (one-shot, not latency-critical) */
    "fetch('/mode?m='+m).catch(function(){});}"

    /* ── volume (one-shot HTTP, not latency-critical) ── */
    "var volTimer=null;"
    "function onVol(v){"
    "document.getElementById('vv').textContent=v+'%';"
    "clearTimeout(volTimer);"
    "volTimer=setTimeout(function(){fetch('/volume?v='+v).catch(function(){});},80);}"

    /* ── language ── */
    "function setLang(l){"
    "['FR','EN','AR'].forEach(function(x){"
    "document.getElementById('l'+x).className='lb'+(x.toLowerCase()===l?' sel':'');});"
    "fetch('/lang?l='+l).catch(function(){});}"

    /* ── joystick state ── */
    "var DEAD=0.14;"
    "var jD=document.getElementById('jD');"
    "var jS=document.getElementById('jS');"
    "jD._dy=0;jS._dx=0;"
    "var lastCmd='',lastTx=0;"
    "function act(){var a=Math.abs(jD._dy),b=Math.abs(jS._dx);"
    "if(a<DEAD&&b<DEAD)return 'stop';"
    "if(a>=b)return jD._dy<0?'forward':'backward';"
    "return jS._dx<0?'spin_left':'spin_right';}"

    /* ── Motor command loop: setInterval (NOT rAF) ── */
    /* setInterval is more reliable on mobile – rAF can be throttled when screen dims */
    "setInterval(function(){"
    "if(mode===0&&wsOk){"
    "var d=document.getElementById('spd').value;"
    "var a=act();"
    "var cmd=a+':'+d;"
    /* Always send if moving (heartbeat keeps 2000ms motor timeout alive) */
    "if(cmd!==lastCmd||a!=='stop'){lastCmd=cmd;"
    "wsSend(a==='stop'?'stop':'mv:'+a+':'+d);}}"
    "},100);"  /* every 100ms → 20 heartbeats in the 2000ms motor timeout window */

    /* ── rAF loop: visual only (joystick stick position smooth update) ── */
    "requestAnimationFrame(function rf(){"
    "requestAnimationFrame(rf);});"  /* keep rAF alive but no motor logic here */

    /* ── joystick touch/mouse ── */
    "function joy(jEl,sEl,ax,ay){"
    "function upd(cx,cy){"
    "var r=jEl.getBoundingClientRect(),H=r.width/2;"
    "var dx=(cx-r.left-H)/H,dy=(cy-r.top-H)/H;"
    "var nd=Math.sqrt(dx*dx+dy*dy);if(nd>1){dx/=nd;dy/=nd;}"
    "jEl._dx=ax?dx:0;jEl._dy=ay?dy:0;"
    "var SR=H*0.64;"
    "sEl.style.transform='translate(calc(-50% + '+(dx*SR)+'px),calc(-50% + '+(dy*SR)+'px))';}"
    "function rst(){jEl._dx=0;jEl._dy=0;sEl.style.transform='translate(-50%,-50%)';}"
    "jEl.addEventListener('touchstart',function(e){"
    "e.preventDefault();var t=e.changedTouches[0];jEl._id=t.identifier;upd(t.clientX,t.clientY);"
    "},{passive:false});"
    "jEl.addEventListener('touchmove',function(e){"
    "e.preventDefault();"
    "for(var i=0;i<e.changedTouches.length;i++){"
    "var t=e.changedTouches[i];if(t.identifier===jEl._id)upd(t.clientX,t.clientY);}}"
    ",{passive:false});"
    "jEl.addEventListener('touchend',function(e){e.preventDefault();rst();},{passive:false});"
    "jEl.addEventListener('touchcancel',function(e){e.preventDefault();rst();},{passive:false});"
    "function mm(e){if(jEl._m)upd(e.clientX,e.clientY);}"
    "function mu(){jEl._m=false;rst();"
    "document.removeEventListener('mousemove',mm);document.removeEventListener('mouseup',mu);}"
    "jEl.addEventListener('mousedown',function(e){"
    "jEl._m=true;upd(e.clientX,e.clientY);"
    "document.addEventListener('mousemove',mm);document.addEventListener('mouseup',mu);});}"
    "joy(jD,document.getElementById('sD'),false,true);"
    "joy(jS,document.getElementById('sS'),true,false);"
    "document.addEventListener('contextmenu',function(e){e.preventDefault();});"
    "</script></body></html>";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static int GetQueryInt(const char* query, const char* key, int default_val) {
    if (!query) return default_val;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char* p = strstr(query, prefix);
    if (!p) return default_val;
    return atoi(p + strlen(prefix));
}

static void GetQueryStr(const char* query, const char* key, char* out, size_t out_len,
                        const char* default_val) {
    if (out_len == 0) return;
    if (!query) { strncpy(out, default_val, out_len-1); out[out_len-1]=0; return; }
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char* p = strstr(query, prefix);
    if (!p) { strncpy(out, default_val, out_len-1); out[out_len-1]=0; return; }
    p += strlen(prefix);
    size_t i = 0;
    while (*p && *p != '&' && i < out_len-1) { out[i++] = *p++; }
    out[i] = 0;
}

// ---------------------------------------------------------------------------
// WebSocket handler  –  PLAY mode motor control (< 5 ms local RTT)
// Protocol (text frames):
//   "mv:<action>:<duty>"   e.g. "mv:forward:60"
//   "stop"
// ---------------------------------------------------------------------------
static esp_err_t HandleWs(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        // WebSocket handshake – accept
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;

    // First call: get frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK || pkt.len == 0 || pkt.len > 63) return ret;

    uint8_t buf[64] = {};
    pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret != ESP_OK) return ret;

    buf[pkt.len] = '\0';
    char* cmd = (char*)buf;

    if (strncmp(cmd, "stop", 4) == 0) {
        MotorDriver::GetInstance().Stop();
        return ESP_OK;
    }

    // "mv:<action>:<duty>"
    if (strncmp(cmd, "mv:", 3) == 0) {
        char* p = cmd + 3;
        char* colon = strchr(p, ':');
        if (!colon) return ESP_OK;
        *colon = '\0';
        const char* action_name = p;
        int duty = atoi(colon + 1);
        if (duty < 1 || duty > 100) duty = 50;

        MotorAction action;
        if (!MotorDriver::ParseActionName(action_name, &action)) return ESP_OK;

        auto& motors = MotorDriver::GetInstance();
        if (motors.IsMotionAllowed()) {
            // Stop() clears queue + cancels timer, then Move() starts fresh.
            // Each WS heartbeat (100ms) calls Stop()+Move(8000ms) → timer always resets
            // before expiring → motor runs INDEFINITELY while joystick held.
            // If WS drops/joystick released → motor stops after max 8000ms (safety).
            motors.Stop();
            motors.Move(action, 8000, MotorStyle::kCommand, duty);
        }
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// HTTP handlers (mode-switch, volume, lang – one-shot, not latency-critical)
// ---------------------------------------------------------------------------
static esp_err_t HandleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kIndexHtml, (ssize_t)sizeof(kIndexHtml) - 1);
    return ESP_OK;
}

static esp_err_t HandleStop(httpd_req_t* req) {
    MotorDriver::GetInstance().Stop();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t HandleMode(httpd_req_t* req) {
    char query[32] = {};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    int m = GetQueryInt(query, "m", -1);
    if (m < 0 || m > 2) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "bad mode");
        return ESP_OK;
    }
    g_mode.store(m);
    MotorDriver::GetInstance().Stop();

    if (m == 0) {
        Application::GetInstance().Schedule([]() {
            Application::GetInstance().StopListening();
        });
        ESP_LOGI(TAG, "Mode -> PLAY (WebSocket)");
    } else if (m == 1) {
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.ToggleChatState();
            }
        });
        ESP_LOGI(TAG, "Mode -> PARLER (Xiaozhi)");
    } else {
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            auto  st  = app.GetDeviceState();
            if (st == kDeviceStateListening || st == kDeviceStateSpeaking) {
                app.StopListening();
            }
            app.SetDeviceState(kDeviceStateIdle);
        });
        ESP_LOGI(TAG, "Mode -> OFF");
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t HandleVolume(httpd_req_t* req) {
    char query[32] = {};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    int v = GetQueryInt(query, "v", -1);
    if (v < 0 || v > 100) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "bad volume");
        return ESP_OK;
    }
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec) codec->SetOutputVolume(v);
    ESP_LOGI(TAG, "Volume -> %d", v);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t HandleLang(httpd_req_t* req) {
    char query[32] = {};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char lang[8];
    GetQueryStr(query, "l", lang, sizeof(lang), "fr");

    const char* instr = nullptr;
    if      (strncmp(lang, "fr", 2) == 0) instr = "Parle uniquement en français dorénavant.";
    else if (strncmp(lang, "en", 2) == 0) instr = "From now on, reply only in English.";
    else if (strncmp(lang, "ar", 2) == 0) instr = "من الآن فصاعداً، أجب باللغة العربية فقط.";
    else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "unknown lang");
        return ESP_OK;
    }

    const std::string msg(instr);
    Application::GetInstance().Schedule([msg]() {
        Application::GetInstance().SendMcpMessage(
            std::string("{\"type\":\"chat\",\"text\":\"") + msg + "\"}");
    });
    ESP_LOGI(TAG, "Lang -> %s", lang);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t HandleStatus(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    const bool moving = MotorDriver::GetInstance().IsMoving();
    auto* codec = Board::GetInstance().GetAudioCodec();
    int vol = codec ? codec->output_volume() : 70;
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"moving\":%s,\"mode\":%d,\"volume\":%d}",
             moving ? "true" : "false", g_mode.load(), vol);
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public: start the server
// ---------------------------------------------------------------------------
void RobotHttpControlStart() {
    if (g_server != nullptr) {
        ESP_LOGW(TAG, "HTTP control server already running");
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port          = 80;
    cfg.max_uri_handlers     = 12;
    cfg.max_open_sockets     = 5;   // WS(1) + HTTP concurrent(4), keep within LWIP limit
    cfg.lru_purge_enable     = true;
    cfg.stack_size           = 6144;

    if (httpd_start(&g_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server on port %d", cfg.server_port);
        return;
    }

    // WebSocket endpoint for real-time PLAY mode (no HTTP overhead)
    static const httpd_uri_t ws_uri = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = HandleWs,
        .user_ctx     = nullptr,
        .is_websocket = true,
    };
    httpd_register_uri_handler(g_server, &ws_uri);

    static const httpd_uri_t uris[] = {
        {"/",       HTTP_GET, HandleIndex,  nullptr},
        {"/stop",   HTTP_GET, HandleStop,   nullptr},
        {"/mode",   HTTP_GET, HandleMode,   nullptr},
        {"/volume", HTTP_GET, HandleVolume, nullptr},
        {"/lang",   HTTP_GET, HandleLang,   nullptr},
        {"/status", HTTP_GET, HandleStatus, nullptr},
    };
    for (auto& u : uris) httpd_register_uri_handler(g_server, &u);

    ESP_LOGI(TAG, "HTTP+WS control server started on port %d", cfg.server_port);
}

// ---------------------------------------------------------------------------
// Public: start SoftAP for low-latency direct connection
// ---------------------------------------------------------------------------
void RobotSoftApStart() {
    ESP_LOGI(TAG, "Starting SoftAP for low-latency control...");

    // 1. Initialize networking (if not already done)
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to init netif: %s", esp_err_to_name(ret));
    }

    // 2. Create the default AP netif
    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGE(TAG, "Failed to create default WiFi AP");
        return;
    }

    // 3. Set a static IP for the AP (192.168.4.1 is typical)
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);
    ip_info.ip.addr = esp_ip4addr_aton("192.168.4.1");
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ip_info.gw.addr = esp_ip4addr_aton("192.168.4.1");
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    // 4. Configure WiFi mode to APSTA (Station + AP)
    wifi_mode_t current_mode;
    if (esp_wifi_get_mode(&current_mode) == ESP_OK) {
        if (current_mode == WIFI_MODE_STA) {
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        } else if (current_mode == WIFI_MODE_NULL) {
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        }
    } else {
        // If not started yet, just set APSTA
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    }

    // 5. Configure the AP settings
    wifi_config_t ap_config = {};
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf((char*)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "RobotControl_%02X%02X", mac[4], mac[5]);
    ap_config.ap.ssid_len = strlen((char*)ap_config.ap.ssid);
    ap_config.ap.channel = 6;
    ap_config.ap.password[0] = 0; // Open network
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    
    ESP_LOGI(TAG, "SoftAP Started. Connect to SSID: %s (No password), IP: 192.168.4.1", ap_config.ap.ssid);
}

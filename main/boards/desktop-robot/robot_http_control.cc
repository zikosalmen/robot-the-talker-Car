#include "robot_http_control.h"
#include "motor_driver.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <esp_http_server.h>
#include <esp_log.h>

static const char* TAG = "RobotHTTP";

// ---------------------------------------------------------------------------
// Embedded HTML gamepad page
// ---------------------------------------------------------------------------
static const char kIndexHtml[] =
    "<!DOCTYPE html><html lang='fr'><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Robot Control</title>"
    "<style>"
    "  *{box-sizing:border-box;margin:0;padding:0}"
    "  body{background:#0d0d1a;color:#e0e0ff;font-family:system-ui,sans-serif;"
    "       display:flex;flex-direction:column;align-items:center;min-height:100vh;"
    "       padding:24px 16px;gap:20px}"
    "  h1{font-size:1.6rem;letter-spacing:.08em;"
    "     background:linear-gradient(90deg,#7c5cfc,#00d4ff);-webkit-background-clip:text;"
    "     -webkit-text-fill-color:transparent}"
    "  #status{font-size:.85rem;opacity:.6}"
    "  .card{background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.1);"
    "        border-radius:16px;padding:20px;width:100%;max-width:380px}"
    "  .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin:16px auto;"
    "        max-width:260px}"
    "  button{background:rgba(124,92,252,.18);border:1px solid rgba(124,92,252,.5);"
    "         border-radius:12px;color:#e0e0ff;font-size:1.6rem;cursor:pointer;"
    "         padding:18px;width:100%;touch-action:manipulation;transition:.15s;"
    "         user-select:none;-webkit-user-select:none}"
    "  button:active{background:rgba(124,92,252,.55);transform:scale(.93)}"
    "  button.stop{background:rgba(252,92,92,.18);border-color:rgba(252,92,92,.5);"
    "              grid-column:span 3;font-size:1rem;padding:14px}"
    "  button.stop:active{background:rgba(252,92,92,.55)}"
    "  .empty{visibility:hidden}"
    "  label{display:block;font-size:.8rem;opacity:.6;margin-bottom:4px}"
    "  input[type=range]{width:100%;accent-color:#7c5cfc}"
    "  .row{display:flex;align-items:center;gap:10px;margin-top:12px}"
    "  .val{min-width:3em;text-align:right;font-size:.95rem}"
    "  #log{font-size:.75rem;opacity:.5;height:60px;overflow-y:auto;margin-top:8px;word-break:break-all}"
    "</style></head><body>"
    "<h1>🤖 Robot Control</h1>"
    "<div id='status'>Connecté · prêt</div>"
    "<div class='card'>"
    "  <div class='grid'>"
    "    <div class='empty'></div>"
    "    <button ontouchstart='mv(\"forward\")' ontouchend='st()' onmousedown='mv(\"forward\")' onmouseup='st()'>&#9650;</button>"
    "    <div class='empty'></div>"
    "    <button ontouchstart='mv(\"spin_left\")' ontouchend='st()' onmousedown='mv(\"spin_left\")' onmouseup='st()'>&#9664;</button>"
    "    <button ontouchstart='mv(\"backward\")' ontouchend='st()' onmousedown='mv(\"backward\")' onmouseup='st()'>&#9660;</button>"
    "    <button ontouchstart='mv(\"spin_right\")' ontouchend='st()' onmousedown='mv(\"spin_right\")' onmouseup='st()'>&#9654;</button>"
    "    <button class='stop' onclick='st()'>⏹ STOP</button>"
    "  </div>"
    "  <div class='row'><label>Durée</label>"
    "    <input type='range' id='ms' min='200' max='8000' value='3000' step='100' oninput='document.getElementById(\"msv\").textContent=this.value+\"ms\"'>"
    "    <span class='val' id='msv'>3000ms</span>"
    "  </div>"
    "  <div class='row'><label>Puissance</label>"
    "    <input type='range' id='duty' min='25' max='80' value='50' step='5' oninput='document.getElementById(\"dutyv\").textContent=this.value+\"%\"'>"
    "    <span class='val' id='dutyv'>50%</span>"
    "  </div>"
    "  <div id='log'></div>"
    "</div>"
    "<script>"
    "let hold=null;"
    "function mv(a){"
    "  clearTimeout(hold);"
    "  const ms=document.getElementById('ms').value;"
    "  const duty=document.getElementById('duty').value;"
    "  fetch('/move?action='+a+'&ms='+ms+'&duty='+duty)"
    "    .then(r=>r.text()).then(t=>log(a+' → '+t)).catch(e=>log('ERR: '+e));"
    "  hold=setTimeout(()=>fetch('/stop'),parseInt(ms)+200);"
    "}"
    "function st(){"
    "  clearTimeout(hold);"
    "  fetch('/stop').then(r=>r.text()).then(t=>log('stop → '+t)).catch(e=>log('ERR: '+e));"
    "}"
    "function log(msg){"
    "  const el=document.getElementById('log');"
    "  el.textContent=msg+'\\n'+el.textContent.slice(0,300);"
    "}"
    "// prevent context menu on long-press mobile"
    "document.addEventListener('contextmenu',e=>e.preventDefault());"
    "</script></body></html>";

// ---------------------------------------------------------------------------
// Helper: extract a query param value from the full URI query string
// Returns atoi of value or default_val if not found.
// ---------------------------------------------------------------------------
static int GetQueryInt(const char* query, const char* key, int default_val) {
    if (!query) return default_val;
    // Build "key=" prefix
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char* p = strstr(query, prefix);
    if (!p) return default_val;
    p += strlen(prefix);
    return atoi(p);
}

static void GetQueryStr(const char* query, const char* key, char* out, size_t out_len,
                        const char* default_val) {
    if (out_len == 0) return;
    if (!query) { strncpy(out, default_val, out_len - 1); out[out_len-1] = 0; return; }
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char* p = strstr(query, prefix);
    if (!p) { strncpy(out, default_val, out_len - 1); out[out_len-1] = 0; return; }
    p += strlen(prefix);
    size_t i = 0;
    while (*p && *p != '&' && i < out_len - 1) { out[i++] = *p++; }
    out[i] = 0;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
static esp_err_t HandleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kIndexHtml, (ssize_t)sizeof(kIndexHtml) - 1);
    return ESP_OK;
}

static esp_err_t HandleMove(httpd_req_t* req) {
    char query[128] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        query[0] = 0;
    }

    char action_name[32];
    GetQueryStr(query, "action", action_name, sizeof(action_name), "forward");
    int ms   = GetQueryInt(query, "ms",   3000);
    int duty = GetQueryInt(query, "duty", 50);

    MotorAction action;
    if (!MotorDriver::ParseActionName(action_name, &action)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Unknown action");
        return ESP_OK;
    }

    auto& motors = MotorDriver::GetInstance();
    if (!motors.IsMotionAllowed()) {
        httpd_resp_sendstr(req, "Battery too low");
        return ESP_OK;
    }

    if (!motors.Move(action, ms, MotorStyle::kCommand, duty)) {
        httpd_resp_sendstr(req, "Queue full");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "HTTP move: %s %d ms %d%%", action_name, ms, duty);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t HandleStop(httpd_req_t* req) {
    MotorDriver::GetInstance().Stop();
    ESP_LOGI(TAG, "HTTP stop");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t HandleStatus(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    const bool moving = MotorDriver::GetInstance().IsMoving();
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"moving\":%s}", moving ? "true" : "false");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public: start the server
// ---------------------------------------------------------------------------
void RobotHttpControlStart() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port    = 80;
    cfg.max_uri_handlers = 8;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static const httpd_uri_t uris[] = {
        {"/",       HTTP_GET, HandleIndex,  nullptr},
        {"/move",   HTTP_GET, HandleMove,   nullptr},
        {"/stop",   HTTP_GET, HandleStop,   nullptr},
        {"/status", HTTP_GET, HandleStatus, nullptr},
    };
    for (auto& u : uris) {
        httpd_register_uri_handler(server, &u);
    }

    ESP_LOGI(TAG, "HTTP control server started on port %d", cfg.server_port);
}

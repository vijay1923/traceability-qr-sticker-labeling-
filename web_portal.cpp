#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "web_portal.h"
#include "diagnostics.h"
#include "state_machine.h"
#include "printer_manager.h"
#include "scanner_manager.h"
#include "shift_manager.h"
#include "config_manager.h"
#include "secrets.h"

// Built-in synchronous WebServer.h, not ESPAsyncWebServer/AsyncTCP - see
// readme.md "Web portal" for why (a real crash, not just a preference:
// AsyncTCP's raw lwIP calls from its own task assert-crash on this
// project's ESP-IDF 5.x / esp32 core 3.3.6). Runs entirely synchronously
// from loop() via webportal_service()'s server.handleClient() call below -
// no separate task, so that whole class of bug can't happen. Tradeoff:
// loop() blocks a few ms while actively handling a request - fine, the
// portal is an occasional tool, not on the scan/print hot path.
static WebServer server(WEBPORTAL_HTTP_PORT);
static bool routes_registered = false;
static bool portal_running = false;

static const char *system_state_to_string(system_state_t s)
{
    switch (s)
    {
        case SYS_WAIT_FOR_START:    return "Waiting for first scan";
        case SYS_INSPECTION_WINDOW: return "Inspection window open";
        case SYS_CYCLE_TIME:        return "Cycle running (printing blocked)";
        default:                    return "Unknown";
    }
}

// ---- Embedded single-page dashboard - self-contained, no external assets,
// since the AP hotspot has no internet access to load a CDN from. No login
// form, no session - anyone who connects to the hotspot sees this
// immediately. See readme.md "Web portal" for that tradeoff. ----
static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>QR Traceability Controller</title>
<style>
  :root {
    --bg: #f4f5f7; --surface: #ffffff; --border: #e2e4e9;
    --text: #1a1d23; --text-muted: #6b7280;
    --success: #16a34a; --danger: #dc2626; --accent: #2563eb;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--bg); color: var(--text);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    padding: 16px;
  }
  .wrap { max-width: 480px; margin: 0 auto; }
  h1 { font-size: 18px; margin: 0 0 2px; }
  .sub { color: var(--text-muted); font-size: 13px; margin: 0; }
  .card {
    background: var(--surface); border: 1px solid var(--border);
    border-radius: 10px; padding: 16px; margin-bottom: 12px;
  }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .stat-label { font-size: 12px; color: var(--text-muted); margin: 0 0 4px; }
  .stat-value { font-size: 16px; font-weight: 600; margin: 0; }
  .ok { color: var(--success); }
  .bad { color: var(--danger); }
  .header-row { margin-bottom: 16px; }
</style>
</head>
<body>
<div class="wrap">

  <div class="header-row">
    <h1>QR Traceability Controller</h1>
    <p class="sub" id="version-line">-</p>
  </div>

  <div class="card">
    <div class="grid">
      <div><p class="stat-label">System state</p><p class="stat-value" id="stat-state">-</p></div>
      <div><p class="stat-label">Printer</p><p class="stat-value" id="stat-printer">-</p></div>
      <div><p class="stat-label">Scanner</p><p class="stat-value" id="stat-scanner">-</p></div>
      <div><p class="stat-label">Current shift</p><p class="stat-value" id="stat-shift">-</p></div>
      <div><p class="stat-label">Shift OK / NG</p><p class="stat-value" id="stat-okng">-</p></div>
      <div><p class="stat-label">Cavity progress</p><p class="stat-value" id="stat-cavity">-</p></div>
    </div>
  </div>

  <p class="sub" style="text-align:center;">Configuration, files, and controls are console-only for now.</p>
</div>

<script>
(function() {
  function refreshStatus() {
    fetch('/api/status')
      .then(function(r) { return r.json(); })
      .then(function(d) {
        document.getElementById('version-line').textContent = 'v' + d.version + ' \u00b7 uptime ' + d.uptime_s + 's';
        document.getElementById('stat-state').textContent = d.system_state;
        var p = document.getElementById('stat-printer');
        p.textContent = d.printer_ok ? 'Connected' : 'Fault';
        p.className = 'stat-value ' + (d.printer_ok ? 'ok' : 'bad');
        var s = document.getElementById('stat-scanner');
        s.textContent = d.scanner_ok ? 'Connected' : 'Disconnected';
        s.className = 'stat-value ' + (d.scanner_ok ? 'ok' : 'bad');
        document.getElementById('stat-shift').textContent = d.shift;
        document.getElementById('stat-okng').textContent = d.shift_ok + ' / ' + d.shift_ng;
        document.getElementById('stat-cavity').textContent = d.printed_count + ' / ' + d.cavity_count;
      })
      .catch(function() { /* transient network hiccup on the hotspot - next 5s refresh will retry */ });
  }

  refreshStatus();
  setInterval(refreshStatus, 5000);
})();
</script>
</body>
</html>
)HTMLPAGE";

static void handle_root()
{
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_status()
{
    StaticJsonDocument<384> doc;
    doc["ok"] = true;
    doc["version"] = FW_VERSION;
    doc["uptime_s"] = (millis() - boot_millis_ref) / 1000UL;
    doc["system_state"] = system_state_to_string(system_state);
    doc["printer_ok"] = !printer_fault_active;
    doc["scanner_ok"] = scanner_connected();
    char shift_str[2] = { current_shift == '\0' ? '?' : current_shift, '\0' };
    doc["shift"] = shift_str;
    doc["shift_ok"] = shift_ok_total;
    doc["shift_ng"] = shift_ng_total;
    doc["printed_count"] = printed_count;
    doc["cavity_count"] = g_cavity_count;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handle_not_found()
{
    server.send(404, "text/plain", "Not found");
}

static void register_routes()
{
    server.on("/", HTTP_GET, handle_root);
    server.on("/api/status", HTTP_GET, handle_status);
    server.onNotFound(handle_not_found);
}

void webportal_start()
{
    if (portal_running)
    {
        Serial.println("Web portal already running");
        return;
    }

    WiFi.mode(WIFI_AP);
    bool ap_ok = WiFi.softAP(AP_SSID, AP_PASSWORD);

    if (!ap_ok)
    {
        Serial.println("ERR - failed to start WiFi AP (check AP_PASSWORD in secrets.h - WPA2 requires 8+ chars)");
        WiFi.mode(WIFI_OFF);
        return;
    }

    if (!routes_registered)
    {
        register_routes();
        routes_registered = true;
    }

    server.begin();
    portal_running = true;

    IPAddress ip = WiFi.softAPIP();
    Serial.println("---- Web portal started ----");
    Serial.print("  SSID:     "); Serial.println(AP_SSID);
    Serial.print("  Password: "); Serial.println(AP_PASSWORD);
    Serial.print("  Connect, then browse to: http://"); Serial.println(ip);
    Serial.println("  No login - anyone on this network can view the dashboard.");
    Serial.println("-----------------------------");
}

void webportal_stop()
{
    if (!portal_running)
    {
        Serial.println("Web portal is not running");
        return;
    }

    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    portal_running = false;

    Serial.println("Web portal stopped");
}

bool webportal_is_running()
{
    return portal_running;
}

void webportal_service()
{
    if (!portal_running)
        return;

    server.handleClient();
}

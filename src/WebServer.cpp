#include "WebServer.h"
#include "Config.h"
#include "Utils.h"

#if WEBSERVER_ENABLED

#include "WebServer.h"
#include "ReticulumNode.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

extern ReticulumNode reticulumNode;

static WiFiServer _server(WEBSERVER_PORT);
static const char* CONFIG_PATH = "/config.json";

static void sendResponse(WiFiClient &client, int code, const char *contentType, const String &body) {
    client.print("HTTP/1.1 ");
    client.print(code);
    client.print(" OK\r\n");
    client.print("Content-Type: "); client.print(contentType); client.print("\r\n");
    client.print("Content-Length: "); client.print(body.length()); client.print("\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);
}

static String readLine(WiFiClient &c, unsigned long timeout=1000) {
    String s;
    unsigned long start = millis();
    while (millis() - start < timeout) {
        if (!c.connected()) break;
        while (c.available()) {
            char ch = c.read();
            s += ch;
            if (s.endsWith("\r\n")) return s;
        }
        delay(1);
    }
    return s;
}

void processHttpClient(WiFiClient &client) {
    // Read request line
    String req = readLine(client);
    if (req.length() == 0) return;
    // Example: "GET /api/v1/status HTTP/1.1\r\n"
    int firstSpace = req.indexOf(' ');
    int secondSpace = req.indexOf(' ', firstSpace + 1);
    if (firstSpace < 0 || secondSpace < 0) return;
    String method = req.substring(0, firstSpace);
    String path = req.substring(firstSpace + 1, secondSpace);

    // Read headers
    int contentLength = 0;
    while (true) {
        String line = readLine(client);
        if (line.length() <= 2) break; // \r\n
        line.trim();
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
        }
    }

    // Read body if present
    String body;
    if (contentLength > 0) {
        unsigned long start = millis();
        while ((int)body.length() < contentLength && millis() - start < 2000) {
            while (client.available() && (int)body.length() < contentLength) {
                body += (char)client.read();
            }
            delay(1);
        }
    }

    // Route handling
    if (method == "GET" && path == "/api/v1/status") {
        DynamicJsonDocument doc(512);
        doc["uptime_s"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["active_links"] = (int)reticulumNode.getLinkManager().getActiveLinkCount();
        doc["route_count"] = (int)reticulumNode.getRoutingTable().getRouteCount();
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);

    } else if (method == "GET" && path == "/api/v1/config") {
        DynamicJsonDocument doc(1024);
        if (SPIFFS.exists(CONFIG_PATH)) {
            File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
            if (!f) { sendResponse(client, 500, "text/plain", "Failed to open config file"); return; }
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (err) { sendResponse(client, 500, "text/plain", "Config parse error"); return; }
        } else {
            doc["node_name"] = "esp32-rns-node";
            JsonObject wifi = doc.createNestedObject("wifi");
            wifi["ssid"] = "";
            wifi["password"] = "";
        }
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);

    } else if (method == "POST" && path == "/api/v1/config") {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendResponse(client, 400, "text/plain", "Invalid JSON"); return; }
#if JSON_CONFIG_ENABLED
        File f = SPIFFS.open(CONFIG_PATH, FILE_WRITE);
        if (!f) { sendResponse(client, 500, "text/plain", "Failed to open config for write"); return; }
        if (serializeJson(doc, f) == 0) { f.close(); sendResponse(client, 500, "text/plain", "Failed to write config"); return; }
        f.close();
#endif
        // Apply WiFi credentials immediately if provided
        if (doc.containsKey("wifi")) {
            JsonObject wifi = doc["wifi"];
            if (wifi.containsKey("ssid") && wifi.containsKey("password")) {
                String ssid = wifi["ssid"].as<String>();
                String pass = wifi["password"].as<String>();
                if (ssid.length() > 0) { DebugSerial.println("WebServer: applying WiFi credentials from config.json"); WiFi.begin(ssid.c_str(), pass.c_str()); }
            }
        }
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);

    } else if (method == "POST" && path == "/api/v1/config/save") {
#if JSON_CONFIG_ENABLED
        if (SPIFFS.exists(CONFIG_PATH)) sendResponse(client, 200, "text/plain", "saved"); else sendResponse(client, 500, "text/plain", "no config to save");
#else
        sendResponse(client, 404, "text/plain", "JSON config disabled");
#endif

    } else if (method == "POST" && path == "/api/v1/restart") {
        sendResponse(client, 200, "text/plain", "restarting");
        delay(250); ESP.restart();

    } else if (method == "GET" && path == "/api/v1/metrics") {
#if METRICS_ENABLED
        DynamicJsonDocument doc(512);
        doc["heap_free"] = ESP.getFreeHeap();
        doc["uptime_s"] = millis() / 1000;
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);
#else
        sendResponse(client, 404, "text/plain", "metrics disabled");
#endif
    } else {
        sendResponse(client, 404, "text/plain", "Not found");
    }
}

void WebServerManager::begin() {
    if (!SPIFFS.begin(true)) DebugSerial.println("WebServer: SPIFFS mount failed"); else DebugSerial.println("WebServer: SPIFFS mounted");
    _server.begin();
    DebugSerial.print("WebServer: started on port "); DebugSerial.println(WEBSERVER_PORT);
}

void WebServerManager::loop() {
    WiFiClient client = _server.available();
    if (client) {
        if (client.connected()) {
            processHttpClient(client);
            delay(1);
            client.stop();
        }
    }
}

bool WebServerManager::loadConfigFromFS(const char* path) {
#if JSON_CONFIG_ENABLED
    if (!SPIFFS.exists(path)) return false;
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return !err;
#else
    (void)path; return false;
#endif
}

bool WebServerManager::saveConfigToFS(const char* path) {
#if JSON_CONFIG_ENABLED
    return SPIFFS.exists(path);
#else
    (void)path; return false;
#endif
}

#else // WEBSERVER_ENABLED

void WebServerManager::begin() { (void)0; }
void WebServerManager::loop() { (void)0; }
bool WebServerManager::loadConfigFromFS(const char* path) { (void)path; return false; }
bool WebServerManager::saveConfigToFS(const char* path) { (void)path; return false; }

#endif // WEBSERVER_ENABLED

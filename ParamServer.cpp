/*  
 * UDP Parameter Server for ESP32
 * https://github.com/jim-mckeown/ESP32-UDP-Parameter-Server-Class
 *
 * MIT License
 * (c) 2026 Jim McKeown
 *
 * Version 2: 
 * Force radio to stay awake to improve UDP reliability
 *
 * Version 3:
 * Add forceSaveParam() function
 *
 */

#include "ParamServer.h"
#include <WiFiManager.h>

ParamServer::ParamServer(unsigned int port) : _port(port) {}
ParamServer::~ParamServer() {}

bool ParamServer::begin(const char* apName) {
    // 1. Initialize File System
    if (!LittleFS.begin(true)) {
        Serial.println("[-] LittleFS Mount Failed");
        return false;
    }

    // 2. Provision WiFi via WiFiManager with Timeout
    WiFiManager wm;
    wm.setConnectTimeout(15); // If stored router is weak/unreachable, fail fast to open portal
    
    Serial.println("[*] Connecting to WiFi...");
    if (!wm.autoConnect(apName)) {
        Serial.println("[-] WiFi Connection timeout reached. Restarting Portal...");
        ESP.restart();
    }
    WiFi.setSleep(false); // force the ESP32 radio to remain active at 100% performance
    Serial.print("[+] Connected! IP address: ");
    Serial.println(WiFi.localIP());

    // 3. Sync Real-Time Clock
    configTime(_gmtOffset_sec, _daylightOffset_sec, _ntpServer);
    
    // 4. Load saved system variables from Flash
    for (const auto& param : _params) {
        loadParam(param);
    }

    // 5. Load stored target phone/tablet IPs
    loadIPList();

    // 6. Bind to network port
    udp.begin(_port);
    Serial.printf("[+] UDP Parameter Server listening on port %d\n", _port);
    return true;
}

void ParamServer::registerParam(const String& command, int* varPtr) { _params.push_back({command, PARAM_INT, varPtr, "/" + command + ".txt"}); }
void ParamServer::registerParam(const String& command, float* varPtr) { _params.push_back({command, PARAM_FLOAT, varPtr, "/" + command + ".txt"}); }
void ParamServer::registerParam(const String& command, String* varPtr) { _params.push_back({command, PARAM_STRING, varPtr, "/" + command + ".txt"}); }

void ParamServer::configureTime(long gmtOffset_sec, int daylightOffset_sec, const char* ntpServer) {
    _gmtOffset_sec = gmtOffset_sec;
    _daylightOffset_sec = daylightOffset_sec;
    _ntpServer = ntpServer;
}

String ParamServer::getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "NTP_ERR_TIME_NOT_SET";
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

void ParamServer::update() {
    int packetSize = udp.parsePacket();
    if (packetSize) {
        char packetBuffer[255];
        int len = udp.read(packetBuffer, 255);
        if (len > 0) packetBuffer[len] = 0;
        String request = String(packetBuffer);
        request.trim();
        
        handlePacket(request, udp.remoteIP(), udp.remotePort());
    }
}

void ParamServer::handlePacket(String packetText, IPAddress remoteIP, uint16_t remotePort) {
    int spaceIndex = packetText.indexOf(' ');
    String cmd = (spaceIndex == -1) ? packetText : packetText.substring(0, spaceIndex);
    String valStr = (spaceIndex == -1) ? "" : packetText.substring(spaceIndex + 1);
    cmd.trim(); valStr.trim();
    // --- Staging New Wi-Fi Network Credentials ---
    static String staged_ssid = "";
    static String staged_pass = "";    

    // Auto-Capture/Refresh client IP address instantly on incoming activity
    addTargetIP(remoteIP.toString());

    // --- Dynamic Target IP Commands ---
    if (cmd.equalsIgnoreCase("add_ip")) {
        addTargetIP(valStr);
        reply(remoteIP, remotePort, "OK add_ip " + valStr);
        return;
    }
    if (cmd.equalsIgnoreCase("remove_ip")) {
        removeTargetIP(valStr);
        reply(remoteIP, remotePort, "OK remove_ip " + valStr);
        return;
    }
    if (cmd.equalsIgnoreCase("list_ip")) {
        String list = "[";
        for (size_t i = 0; i < _targetIPs.size(); i++) {
            list += _targetIPs[i] + (i < _targetIPs.size() - 1 ? ", " : "");
        }
        list += "]";
        reply(remoteIP, remotePort, "OK list_ip " + list);
        return;
    }

    if (cmd.equalsIgnoreCase("new_ssid")) {
        staged_ssid = valStr;
        reply(remoteIP, remotePort, String("OK new_ssid ") + (valStr == "" ? "[NULL_STRING]" : valStr));
        return;
    }

    if (cmd.equalsIgnoreCase("new_pass")) {
        staged_pass = valStr;
        reply(remoteIP, remotePort, String("OK new_pass ") + (valStr == "" ? "[NULL_STRING]" : "********"));
        return;
    }

    if (cmd.equalsIgnoreCase("commit_wifi")) {
        if (staged_ssid == "") {
            // If SSID is explicitly set to a null string, wipe memory and prepare portal
            reply(remoteIP, remotePort, "OK clearing_credentials_and_opening_portal");
            delay(1000);
            WiFiManager wm;
            wm.resetSettings();
            ESP.restart();
            return;
        } else {
            // Stage credentials into the standard ESP32 WiFi library stack
            reply(remoteIP, remotePort, "OK migrating_to_new_network: " + staged_ssid);
            delay(1000);
            
            // Forces the ESP32 underlying SDK to save these credentials to its persistent system partition
            WiFi.begin(staged_ssid.c_str(), staged_pass.c_str());
            
            // Give it a brief moment to write to internal memory, then cycle the board
            delay(2000); 
            ESP.restart();
            return;
        }
    }
    // --- Hard Factory Network Reset ---
    if (cmd.equalsIgnoreCase("forget_wifi")) {
        reply(remoteIP, remotePort, "OK system_rebooting");
        delay(1000);
        WiFiManager wm;
        wm.resetSettings();
        ESP.restart();
        return;
    }

    // --- Unified Core Get / Set Parameter Engine ---
    for (auto& param : _params) {
        if (param.command.equalsIgnoreCase(cmd)) {
            // Getter Handler (?)
            if (valStr == "?") {
                String response = "OK " + param.command + " ";
                if (param.type == PARAM_INT) response += *(int*)param.varPtr;
                if (param.type == PARAM_FLOAT) response += String(*(float*)param.varPtr, 4);
                if (param.type == PARAM_STRING) response += *(String*)param.varPtr;
                reply(remoteIP, remotePort, response);
                return;
            }

            // Setter Handler
            if (param.type == PARAM_INT) *(int*)param.varPtr = valStr.toInt();
            else if (param.type == PARAM_FLOAT) *(float*)param.varPtr = valStr.toFloat();
            else if (param.type == PARAM_STRING) *(String*)param.varPtr = valStr;

            saveParam(param); // Persist updated value directly to LittleFS
            reply(remoteIP, remotePort, "OK " + param.command + " " + valStr);
            return;
        }
    }

    reply(remoteIP, remotePort, "ERR Unknown command or parameter");
}

void ParamServer::reply(IPAddress ip, uint16_t port, String msg) {
    udp.beginPacket(ip, port);
    udp.print(msg);
    udp.endPacket();
}

void ParamServer::sendAlert(const String& alertMessage) {
    String formattedMsg = "[" + getFormattedTime() + "] ALERT: " + alertMessage;
    
    // Unicast Sweep Target Iterator Loop
    for (const String& ipStr : _targetIPs) {
        IPAddress target;
        if (target.fromString(ipStr)) {
            udp.beginPacket(target, 9999); // Fires directly into Termux listening port
            udp.print(formattedMsg);
            udp.endPacket();
        }
    }
}

void ParamServer::addTargetIP(const String& ipStr) {
    if (ipStr.length() < 7) return; 
    for (const auto& ip : _targetIPs) {
        if (ip == ipStr) return; // Deduplicate
    }
    _targetIPs.push_back(ipStr);
    saveIPList();
    Serial.println("[+] Target IP Added: " + ipStr);
}

void ParamServer::removeTargetIP(const String& ipStr) {
    for (auto it = _targetIPs.begin(); it != _targetIPs.end(); ++it) {
        if (*it == ipStr) {
            _targetIPs.erase(it);
            saveIPList();
            Serial.println("[-] Target IP Removed: " + ipStr);
            return;
        }
    }
}

void ParamServer::saveIPList() {
    File file = LittleFS.open("/iplist.cfg", "w");
    if (!file) return;
    for (const auto& ip : _targetIPs) {
        file.println(ip);
    }
    file.close();
}

void ParamServer::loadIPList() {
    if (!LittleFS.exists("/iplist.cfg")) return;
    File file = LittleFS.open("/iplist.cfg", "r");
    if (!file) return;
    while (file.available()) {
        String ip = file.readStringUntil('\n');
        ip.trim();
        if (ip.length() >= 7) _targetIPs.push_back(ip);
    }
    file.close();
}

void ParamServer::saveParam(const Parameter& param) {
    File file = LittleFS.open(param.filename, "w");
    if (!file) return;
    if (param.type == PARAM_INT) file.print(*(int*)param.varPtr);
    if (param.type == PARAM_FLOAT) file.print(*(float*)param.varPtr, 6);
    if (param.type == PARAM_STRING) file.print(*(String*)param.varPtr);
    file.close();
}

void ParamServer::loadParam(const Parameter& param) {
    if (!LittleFS.exists(param.filename)) return;
    File file = LittleFS.open(param.filename, "r");
    if (!file) return;
    String content = file.readString();
    content.trim();
    file.close();

    if (param.type == PARAM_INT) *(int*)param.varPtr = content.toInt();
    if (param.type == PARAM_FLOAT) *(float*)param.varPtr = content.toFloat();
    if (param.type == PARAM_STRING) *(String*)param.varPtr = content;
}

void ParamServer::forceSaveParam(const String& command) {
    for (const auto& param : _params) {
        if (param.command.equalsIgnoreCase(command)) {
            saveParam(param);
            Serial.println("[+] ParamServer manually persisted parameter: " + command);
            return;
        }
    }
}
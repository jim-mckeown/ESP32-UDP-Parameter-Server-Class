#ifndef PARAM_SERVER_H
#define PARAM_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>

// Supported parameter datatypes
enum ParamType {
    PARAM_INT,
    PARAM_FLOAT,
    PARAM_STRING
};

// Structure to bind a command string to a physical runtime variable
struct Parameter {
    String command;
    ParamType type;
    void* varPtr;
    String filename; // Flash storage path
};

class ParamServer {
private:
    WiFiUDP udp;
    unsigned int _port;
    std::vector<Parameter> _params;
    std::vector<String> _targetIPs; // Dynamic unicast alert targets
    
    const char* _ntpServer = "pool.ntp.org";
    long _gmtOffset_sec = 0;
    int _daylightOffset_sec = 0;

    void handlePacket(String packetText, IPAddress remoteIP, uint16_t remotePort);
    void reply(IPAddress ip, uint16_t port, String msg);
    
    // Parameter Persistence
    void saveParam(const Parameter& param);
    void loadParam(const Parameter& param);
    
    // Target IP Persistence/Management
    void saveIPList();
    void loadIPList();
    void addTargetIP(const String& ipStr);
    void removeTargetIP(const String& ipStr);

public:
    ParamServer(unsigned int port = 8888);
    ~ParamServer();

    // Initialization & Lifecycle
    bool begin(const char* apName = "ESP32_Config_AP");
    void update();

    // Overloaded Parameter Registration
    void registerParam(const String& command, int* varPtr);
    void registerParam(const String& command, float* varPtr);
    void registerParam(const String& command, String* varPtr);

    // Clock Configurations
    void configureTime(long gmtOffset_sec, int daylightOffset_sec, const char* ntpServer = "pool.ntp.org");
    String getFormattedTime();

    // Direct Unicast Push Alert Dispatcher
    void sendAlert(const String& alertMessage);
};

#endif
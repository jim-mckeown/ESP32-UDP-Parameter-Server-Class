# ESP32-UDP-Parameter-Server-Class
ParamServer is a general-purpose, non-blocking ESP32 class that encapsulates functionality for listening for, processing, and responding to UDP configuration commands on a local network. It simplifies remote parameter tuning, automated client IP tracking, network provisioning, and real-time alerts.

## Core Capabilities

* **Type-Agnostic Variable Linking:** Easily bind standard runtime runtime variables (`int`, `float`, `String`) to custom string network commands.
* **Automatic Persistent Storage:** Uses **LittleFS** to automatically save variable updates to the onboard flash memory and automatically reloads them at boot.
* **Zero-Config Client IP Tracking:** Automatically captures and logs the IP address of any client that sends a packet to it, enabling dynamic, direct unicast alert messaging without manual IP assignment.
* **Smart WiFi Provisioning:** Employs **WiFiManager** with a 15-second fail-fast timeout. If the stored network is weak or missing, it spins up a localized configuration Access Point.
* **Onboard Real-Time Clock:** Synchronizes natively with an NTP server at boot to provide precise timestamps for system logs and outgoing alerts.

---

## 1. Network API Command Reference

All commands follow the `'parameter value'` plain-text protocol. Responses are prefixed with `OK` or `ERR`.

### Dynamic Parameters (Registered via Sketch)

These commands are dynamically generated depending on what variables you register in your main code loop:

* **Get Parameter Value (`?`)**
* *Syntax:* `<command_name> ?`
* *Example Request:* `min_weight ?`
* *Example Response:* `OK min_weight 1.0000`


* **Set Parameter Value**
* *Syntax:* `<command_name> <value>`
* *Example Request:* `min_weight 2.5`
* *Example Response:* `OK min_weight 2.5` *(Value is instantly committed to LittleFS Flash)*

### Local Storage Overrides (Programmatic Commits)

If a registered parameter is modified locally within the C++ sketch logic (rather than via a remote UDP network network command), it will reside only in temporary RAM. To explicitly save its current state to LittleFS so that it survives a device reboot, use:

* **Force Manual Save:** `server.forceSaveParam(const String& command);`
* *Example Usage:* `server.forceSaveParam("threshold");`
* *Behavior:* Commits the current runtime value of the variable bound to `"threshold"` directly to flash storage (`/threshold.txt`).

### Target IP Management Commands

Used to check or modify the list of client devices (e.g., phones/tablets running Termux) that receive high-priority direct alert pushes.

* **List Target IPs:** `list_ip ?`
* *Response:* `OK list_ip [192.168.1.50, 192.168.1.51]`


* **Manually Add Target IP:** `add_ip <ip_address>`
* *Request:* `add_ip 192.168.1.100`
* *Response:* `OK add_ip 192.168.1.100`


* **Manually Remove Target IP:** `remove_ip <ip_address>`
* *Request:* `remove_ip 192.168.1.100`
* *Response:* `OK remove_ip 192.168.1.100`



> **Note on Auto-Tracking:** You rarely need to call `add_ip`. Sending *any* generic command (like `status ?`) automatically registers the sender's IP address into the alert recipient database list.

### Wi-Fi Configuration & Staging Commands

Allows you to cleanly migrate the device to an entirely different router network remotely before physically relocating the hardware.

* **Stage New Network SSID:** `new_ssid <ssid_name>`
* *Request:* `new_ssid Alternate_Router_WiFi`
* *Response:* `OK new_ssid Alternate_Router_WiFi`


* **Stage New Network Password:** `new_pass <password>`
* *Request:* `new_pass SecretPassword123`
* *Response:* `OK new_pass ********`


* **Commit Changes & Reboot:** `commit_wifi 1`
* *Response:* `OK migrating_to_new_network: Alternate_Router_WiFi`
* *Behavior:* Flashes the new credentials directly into the system SDK partition and reboots the ESP32 to connect.


* **Force Factory Reset / Erase Credentials:** `new_ssid ` *(Send with trailing space, no parameter)* followed by `commit_wifi 1`.
* *Response:* `OK clearing_credentials_and_opening_portal`
* *Behavior:* Wipes all stored Wi-Fi memory and opens the onboarding captive portal setup screen on next boot.



---

## 2. Developer Quick-Start C++ Blueprint

To utilize the class in an empty project sketch, follow this structural workflow:

```cpp
#include <Arduino.h>
#include "ParamServer.h"

// 1. Instantiate on target port
ParamServer server(8888);

// 2. Define global variables with default values
int system_status = 0;
float calibration_factor = 1.00;
String device_name = "Room_4_Monitor";

void setup() {
    Serial.begin(115200);

    // 3. (Optional) Configure Timezone prior to starting server (e.g. EST: -5 Hours)
    server.configureTime(-18000, 3600, "pool.ntp.org");

    // 4. Bind commands to variable addresses
    server.registerParam("status", &system_status);
    server.registerParam("cal", &calibration_factor);
    server.registerParam("name", &device_name);

    // 5. Fire up the network layer. Auto-loads saved configuration parameters from Flash.
    // Falls back to "ESP32_Config_AP" portal if connection fails.
    if (!server.begin("ESP32_Config_AP")) {
        Serial.println("System Core Failure");
        while(1);
    }
}

void loop() {
    // 6. Keep the underlying UDP socket engine listening and processing
    server.update();

    // 7. Push real-time alerts instantly to all targets (fires out to client listening port 9999)
    if (/* trigger condition */) {
        server.sendAlert("EVENT_OCCURRED");
    }

    // Example Scenario: Automated Self-Calibration or Local Emergency Fallback
    if (unusual_environmental_noise_detected) {
        threshold = 210.0; // Adjusted locally in RAM 
        
        // CRITICAL: Because this was changed via local code rather than a UDP packet, 
        // it won't auto-save to LittleFS. We must manually force the commit:
        server.forceSaveParam("threshold"); 
        Serial.println("[System] Local adjustment permanently committed to Flash storage.");
    }

    yield(); // Maintain ESP32 thread health
}

```

---

## 3. Storage Architecture

The class uses **LittleFS** to map variable updates directly to individual files in the root flash path.

* A parameter registered as `"cal"` generates a file named `/cal.txt`.
* The target IP list is tracked and stored inside `/iplist.cfg`.

**Preserving Memory Lifespans:** To protect the flash memory against premature degradation, variables are only written to flash storage automatically during explicit network *Setter commands*. Routine code loop adjustments do not stress storage components. **To intentionally save a locally altered variable state from within your code, you must invoke `server.forceSaveParam("command_name");`.**


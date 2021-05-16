# ESP-BLE-MESH-LPN-THERMOMETER
Example of thermometer made on ESP32 with ESP-IDF communicating via Bluetooth mesh
Based on examples delivered with ESP-IDF

Implemented Sensor and Battery server.

Proxy server is turned off by default.
In order to communicate with smartphone app, after provisioning, another node with proxy server is required.
In order to enable proxy feature, low power feature should be turned off and corresponding box checked in sdkconfig.

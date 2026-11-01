#pragma once

/**
 * @brief Start the HTTP manual control server on port 80.
 *
 * Exposes:
 *   GET /          → HTML gamepad page
 *   GET /move      → ?action=forward|backward|spin_left|spin_right|turn_around
 *                    &ms=<0-8000>  &duty=<25-80>
 *   GET /stop      → stop motors immediately
 *   GET /status    → JSON {"moving":true|false}
 *
 * Call once after Wi-Fi is connected (from the board constructor or Start()).
 */
void RobotHttpControlStart();

/**
 * @brief Enable SoftAP so the phone can connect directly to the ESP32.
 *
 * Creates a WiFi network "RobotControl" (open, no password) at 192.168.4.1.
 * The same HTTP+WS server on port 80 is reachable on this interface.
 * WiFi STA (Xiaozhi cloud) continues to work in parallel (APSTA mode).
 *
 * Call once after Wi-Fi STA has been initialized.
 */
void RobotSoftApStart();

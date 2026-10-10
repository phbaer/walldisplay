# LittleFS storage acceptance

Use a test panel with backed-up configuration and a provisioned screenshot token. Keep MQTT/HTTP access on its trusted LAN. Record firmware versions and partition tables; never erase NVS for this check.

1. Start with SPIFFS firmware and a named capture. Install the LittleFS application through the normal signed OTA path without changing the partition table. Confirm MQTT reconnects and Wi-Fi, MQTT, token, and panel settings are preserved. The first screenshot initialization should format only `storage`.
2. Capture the same name twice with visibly different page contents. Download with bearer authentication each time; check valid 480×480 BMPs and changed pixels. Confirm the old file is replaced, and capture four names plus a fifth to check bounded retention.
3. Measure capture and HTTP download time across at least ten repeated captures. Navigate, run media updates, and watch for display artifacts or stalls during writes. Compare against SPIFFS on the same panel.
4. Interrupt power during a capture, then restart. Confirm the filesystem mounts and subsequent captures/downloads work. Repeat during replacement and during the first format. Captures are deliberately cleaned on boot; credentials must survive.
5. Roll back to the original SPIFFS application. Confirm its mount-failure formatting restores screenshot storage and configuration survives. Upgrade to LittleFS again and repeat capture/download.
6. Check disabled screenshots and missing/incorrect bearer tokens still prevent capture/download as before.

Status: native filesystem tests and firmware compilation are automated; physical power interruption, display timing, and OTA upgrade/rollback require the test-panel procedure above.

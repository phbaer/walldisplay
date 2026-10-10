# LittleFS screenshot storage

Branch: `feature/littlefs-storage`, based on `main` at `120b953`.
Selected release: **1.2.0**, explicitly selected by the maintainer to share the release with the screenshot improvements (overriding the workflow’s migration classification).
Original MQTT contract: **10**, unchanged by LittleFS; the rebase retains main’s newer contract as recorded below. Keep the release selection fixed for this branch.
Rebased onto `main` at `af94b5f`, retaining its MQTT contract **11** and screenshot diagnostics/token controls. Release **1.2.0** remains unchanged.

Only transient screenshot storage changes. Mount by the existing `storage` label and retain its legacy subtype, offset, and size so application-only OTA and older SPIFFS rollback firmware can both find it. Format automatically on mount failure, as the old firmware did. Credentials and configuration remain in NVS. Close the temporary BMP before atomic rename replacement; never unlink the destination first on LittleFS.

Pin the ESP-IDF LittleFS component and exercise its actual filesystem implementation with a native flash simulator for rename replacement, interrupted writes, and remount recovery. Firmware compilation verifies ESP-IDF integration. Hardware acceptance covers timing, display responsiveness, and upgrade/rollback.

The rebase preserves screenshot error-stage reporting, token controls, preview support, and device entities. The writer closes the temporary file and renames directly over the destination, without the SPIFFS-specific destination unlink. LittleFS-backed writer/fault tests replace the SPIFFS-conflict regression test and accept the diagnostic stage parameter. The README retains main’s device entities and contract metadata and documents storage initialization at boot, including panels whose token is configured later.

Validation: ESP-IDF 6.0.2 build, 57 Python tests (plus 19 subtests) (including actual LittleFS replacement/power-cut simulation and the full-size firmware BMP writer), native unit tests, blueprint YAML parsing, component/version checks, diff checks, and OTA stack guard passed. LVGL remains locked to 9.5.0; only the LittleFS component was added. Hardware acceptance is documented but not performed; no firmware was installed on the panel.

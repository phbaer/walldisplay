# Configurable pages

Feature base: `e2fcb46` on `main`. Branch: `feat/configurable-pages`.
Selected firmware/blueprint/integration release: **0.6.0** (backward-compatible feature).
Selected MQTT contract: **9** (five ordered page slots and dedicated page navigation layout).
Keep these values for every refinement on this branch.

## Scope and implementation order

1. Introduce a bounded layout model: one instance of each built-in page; up to five ordered enabled slots, configurable titles, and startup page. Validate before saving, preserve the last valid configuration, migrate the previous startup preference, and reuse a saved layout on reboot.
2. Bind the UI to this model and add a six-control buttons grid independent of the footer. Retain shared header/footer behaviour and hide navigation for a single page.
3. Expose equivalent blueprint YAML inputs and Home Assistant selectors. Add full configuration copy/paste to integration setup/options, using the same validation and state as the forms. Preserve the two synchronization paths and their existing action/event distinction.
4. Supply complete YAML examples, update the README and release/contract metadata, and synchronize the integration deployment copy.
5. Validate firmware, native layout tests, Home Assistant schema/flow/runtime tests, blueprint YAML and payload parity, generated examples, and diff hygiene. Commit the verified implementation.

## Deliberate limits

- One instance of each built-in page. The layout reserves five ordered slots; additional page types and dynamically allocated page instances are future work.
- YAML round trips in the integration editor; no automatically watched external YAML file or direct `configuration.yaml` integration block. The blueprint supports ordinary automation YAML. Both can be edited through Home Assistant UI.
- Blueprint actions/wake triggers remain action sequences/trigger selectors. Integration action-only controls emit standard event entities and require manual automations.
- Page manager extraction covers configuration/navigation policy. Weather rendering and media controls are not fully extracted into independent widgets in this release.
- Hardware rendering, touch and reboot behaviour require a panel test after flashing; local builds and host tests do not replace that check.

## Validation result

Implementation steps 1–4 are complete. ESP-IDF 6.0.2 firmware build passed with 23% application-partition space free. Native power-policy and page-manager tests passed. Ten Python tests passed against Home Assistant 2026.9.2, covering full YAML round trips, generated build defaults, flow validation, grid toggles/events, unavailable controls, matching MQTT payloads, and expanded blueprint automation schema. Blueprint/workflow/example YAML, local documentation links, deployable-copy consistency, and diff whitespace checks passed.

No panel was flashed. Physical layout, touch, NVS reboot recovery, and live Home Assistant frontend interaction remain hardware/deployment verification steps.

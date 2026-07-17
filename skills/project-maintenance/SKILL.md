---
name: project-maintenance
description: Maintain this repository’s documentation, release versions for firmware or Home Assistant blueprint changes, and MQTT contract. Use for every code, configuration, firmware, Home Assistant blueprint, architecture, logic, or documentation change in this repository.
---

# Project Maintenance

Apply this workflow to every change.

1. Read `AGENTS.md` and the relevant README sections before editing.
2. If the change affects firmware or the Home Assistant blueprint, classify it with semantic versioning:
   - major for incompatible behavior or migrations;
   - minor for backward-compatible capabilities or architectural features;
   - patch for fixes, refactors, documentation, and maintenance.
3. Only for a firmware or Home Assistant blueprint change, bump `APP_FW_VERSION` in `include/walldisplay/app_config.h` and the blueprint release version in `config/blueprints/automation/walldisplay/mqtt_sync.yaml`. Do not bump either version for workflow-only, documentation-only, or other repository maintenance changes.
4. When an MQTT topic, payload, discovery entity, or semantic behavior changes, also increment the MQTT contract version in both files. Update contract compatibility documentation and discovery diagnostics.
5. Update `README.md` in the same change. Describe the current behavior, interfaces, configuration, and operational consequences; remove obsolete descriptions.
6. Validate proportionately:
   - firmware: `idf.py build`;
   - blueprint: parse YAML while accepting the Home Assistant `!input` tag;
   - all changes: `git diff --check` and review the working tree.
7. When an implementation step is complete and its changes are verified, commit it if appropriate. Use a short, descriptive message and keep unrelated changes separate.

Do not introduce vendor-specific instructions, tool names, or metadata into these repository rules. Keep instructions in standard Markdown so any assistant can follow them.

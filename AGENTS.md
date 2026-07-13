# Repository Rules for AI Assistants

These rules apply to every AI assistant, LLM, code-generation tool, and automated contributor working in this repository.

1. Treat [README.md](README.md) as the project’s primary technical reference. Keep it accurate and complete enough to be the first source consulted before making a change.
2. Update the README in the same change as every architecture, logic, interface, configuration, build, firmware, Home Assistant blueprint, or operational-workflow change. Do not defer documentation updates.
3. Bump release versions only when a change affects firmware or the Home Assistant blueprint, using semantic versioning:
   - major: incompatible behavior, migration, or breaking contract change;
   - minor: backward-compatible feature or architectural capability;
   - patch: bug fix, documentation, refactor, or maintenance change.
4. For a firmware or Home Assistant blueprint release, keep `APP_FW_VERSION` and the Home Assistant blueprint version aligned. Bump the MQTT contract version in both places whenever MQTT topics, payloads, discovery entities, or their semantics change.
5. Before handoff, validate the affected artifacts: build firmware changes, parse blueprint YAML changes, run documentation and diff checks, and report anything that could not be verified.

Use [skills/project-maintenance/SKILL.md](skills/project-maintenance/SKILL.md) for the required maintenance workflow.

# Repository Rules for AI Assistants

These rules apply to every AI assistant, LLM, code-generation tool, and automated contributor working in this repository.

1. Treat [README.md](README.md) as the project’s primary technical reference. Keep it accurate and complete enough to be the first source consulted before making a change.
2. Update the README in the same change as every architecture, logic, interface, configuration, build, firmware, Home Assistant blueprint, or operational-workflow change. Do not defer documentation updates.
3. Bump release versions only when a change affects firmware or the Home Assistant blueprint, using semantic versioning:
   - major: incompatible behavior, migration, or breaking contract change;
   - minor: backward-compatible feature or architectural capability;
   - patch: bug fix, documentation, refactor, or maintenance change.
4. For a firmware or Home Assistant blueprint release, keep `APP_FW_VERSION` and the Home Assistant blueprint version aligned. Bump the MQTT contract version in both places whenever MQTT topics, payloads, discovery entities, or their semantics change.
   - For one cohesive feature branch, increment the release and MQTT contract versions at most once. Follow-up fixes and refinements in that branch retain the feature's already-selected versions.
5. Before handoff, validate the affected artifacts: build firmware changes, parse blueprint YAML changes, run documentation and diff checks, and report anything that could not be verified.
6. Keep both Home Assistant delivery paths supported and aligned: the MQTT Sync blueprint and the `walldisplay_sync` custom integration are maintained alternatives, not a migration path that retires either one. When changing panel configuration, MQTT synchronization, or user-visible behavior, update the equivalent path in both and document any intentional difference in the README. Do not run both paths for the same panel topic, because they would process and publish the same state and commands twice. The blueprint owns user-defined action sequences and wake-up trigger selectors; the integration deliberately exposes standard entities and events, so its README documentation must explicitly state that users need manual automations for those behaviors.

Use [skills/project-maintenance/SKILL.md](skills/project-maintenance/SKILL.md) for the required maintenance workflow.

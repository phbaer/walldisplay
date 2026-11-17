# Documentation index

The [README](../README.md) is the project overview and the first source to
consult. Each fact has one home; other documents link to it instead of
repeating it. Documents are grouped by purpose:

## Reference

- [MQTT contract and Home Assistant entities](reference/mqtt-contract.md):
  release and contract versions, topics, payloads, discovery entities,
  screenshots.
- Configuration examples: [panel_config.example.yaml](../config/panel_config.example.yaml),
  [panel_blueprint.yaml](../config/examples/panel_blueprint.yaml),
  [panel_integration.yaml](../config/examples/panel_integration.yaml),
  [mosquitto.conf.example](../config/mosquitto.conf.example),
  [mosquitto-acl.example](../config/mosquitto-acl.example).

## How-to guides

- [Security configuration and firmware signing](how-to/security-and-signing.md)
- [Test builds and releases](how-to/build-and-release.md)
- [SoftAP provisioning acceptance](acceptance/softap-provisioning.md)
- [Capturing panel screenshots](screenshots/README.md)

## Explanation

- [Hardware, display pipeline, and firmware updates](explanation/hardware-and-updates.md)
- Architecture: the [README architecture section](../README.md#architecture).
- Implementation notes with fixed release selections and verification status:
  [IPv6](plans/ipv6.md), [setup portal hardening](plans/setup-portal-hardening.md),
  [firmware structure](plans/firmware-structure.md),
  [security and reliability](plans/security-reliability.md),
  [configurable pages](plans/configurable-pages.md),
  [About page](plans/about-page.md),
  [screenshot diagnostics](plans/ha-screenshot-diagnostics.md),
  [SoftAP provisioning handover](plans/softap-provisioning-handover.md).

Getting started (installation, build, initial setup) is in the README.

"""Attach native entities to the MQTT-owned panel without creating a second device."""
from __future__ import annotations

import logging

from homeassistant.core import callback
from homeassistant.helpers import device_registry as dr, entity_registry as er

from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)


def async_setup_device_link(hass, entry) -> None:
    """Migrate on setup and when discovery/entities arrive, preserving the MQTT UUID."""
    devices = dr.async_get(hass)
    entities = er.async_get(hass)
    runtime = entry.runtime_data
    identifiers = runtime.device_identifiers
    legacy_identifier = (DOMAIN, runtime.identifier)

    def reconcile_devices() -> None:
        targets_by_id = {
            device.id: device for config in hass.config_entries.async_entries("mqtt")
            for device in dr.async_entries_for_config_entry(devices, config.entry_id)
            if device.identifiers & identifiers
        }
        targets = list(targets_by_id.values())
        if len(targets) != 1:
            # MQTT discovery can arrive after integration setup. Keep old devices until then.
            return
        target = targets[0]
        legacy = [device for device in dr.async_entries_for_config_entry(devices, entry.entry_id)
                  if device.id != target.id
                  and (legacy_identifier in device.identifiers or device.identifiers & identifiers)]
        for entity in er.async_entries_for_config_entry(entities, entry.entry_id):
            if entity.device_id != target.id:
                entities.async_update_entity(entity.entity_id, device_id=target.id)
        for device in legacy:
            if er.async_entries_for_device(entities, device.id, include_disabled_entities=True):
                continue
            # Never remove a device owned by another integration or a parent of other devices.
            if device.config_entries != {entry.entry_id} or any(
                child.via_device_id == device.id
                for config in hass.config_entries.async_entries()
                for child in dr.async_entries_for_config_entry(devices, config.entry_id)
            ):
                continue
            if hasattr(dr, "async_entries_for_parent_device") and dr.async_entries_for_parent_device(devices, device.id):
                continue
            devices.async_remove_device(device.id)
            _LOGGER.info("Removed obsolete WallDisplay device %s; retained MQTT device %s", device.id, target.id)

    in_progress = False

    @callback
    def reconcile(_event=None) -> None:
        nonlocal in_progress
        if in_progress:
            return
        in_progress = True
        try:
            reconcile_devices()
        finally:
            in_progress = False

    entry.async_on_unload(hass.bus.async_listen(dr.EVENT_DEVICE_REGISTRY_UPDATED, reconcile))
    entry.async_on_unload(hass.bus.async_listen(er.EVENT_ENTITY_REGISTRY_UPDATED, reconcile))
    reconcile()

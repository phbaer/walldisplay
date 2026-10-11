"""Exercise device consolidation against Home Assistant's real registries."""
import asyncio
import pytest
from types import SimpleNamespace
from unittest.mock import patch

from homeassistant.core import HomeAssistant
from homeassistant.config_entries import ConfigEntry
from homeassistant.helpers import device_registry as dr, entity_registry as er

from custom_components.walldisplay_sync.device import async_setup_device_link
from custom_components.walldisplay_sync.runtime import WallDisplayRuntime


@pytest.mark.parametrize("discovery_first", [True, False])
def test_device_migration_preserves_mqtt_uuid_and_entity_ids(tmp_path, discovery_first):
    async def run():
        hass = HomeAssistant(str(tmp_path))
        entries = {}
        for domain in ('mqtt', 'walldisplay_sync', 'other'):
            entry = ConfigEntry(domain=domain, title=domain, unique_id='panel/test', version=1,
                                minor_version=1, data={}, options={}, source='user',
                                discovery_keys={}, subentries_data=[])
            entries[entry.entry_id] = entry
        hass.config_entries = SimpleNamespace(
            async_get_entry=entries.get,
            async_entries=lambda domain=None: [e for e in entries.values() if domain is None or e.domain == domain],
        )
        mqtt_entry, entry, other_entry = entries.values()
        entry.runtime_data = WallDisplayRuntime(hass, 'panel/test', 'Panel', 'panel/test')
        dr.async_setup(hass)
        await dr.async_load(hass)
        await er.async_load(hass)
        devices, entities = dr.async_get(hass), er.async_get(hass)
        with patch.object(devices, 'async_schedule_save'), patch.object(entities, 'async_schedule_save'):
            def create_target():
                return devices.async_get_or_create(config_entry_id=mqtt_entry.entry_id,
                                                    identifiers=entry.runtime_data.device_identifiers, name='Panel')
            target = create_target() if discovery_first else None
            legacy = devices.async_get_or_create(config_entry_id=entry.entry_id,
                                                 identifiers={('walldisplay_sync', 'panel/test')}, name='Legacy')
            duplicate = devices.async_get_or_create(config_entry_id=entry.entry_id,
                                                    identifiers=entry.runtime_data.device_identifiers, name='Duplicate')
            native = entities.async_get_or_create('button', 'walldisplay_sync', 'panel/test_wake',
                                                  config_entry=entry, device_id=legacy.id)
            disabled = entities.async_get_or_create('sensor', 'walldisplay_sync', 'panel/test_hostname',
                                                    config_entry=entry, device_id=duplicate.id,
                                                    disabled_by=er.RegistryEntryDisabler.USER)
            if target is None:
                async_setup_device_link(hass, entry)
                assert devices.async_get(legacy.id) is not None
                assert entities.async_get(native.entity_id).device_id == legacy.id
                target = create_target()
            mqtt_entity = entities.async_get_or_create('sensor', 'mqtt', 'panel_ip',
                                                       config_entry=mqtt_entry, device_id=target.id)
            devices.async_update_device(target.id, name_by_user='My physical panel')
            target_id = target.id
            entity_ids = [native.entity_id, disabled.entity_id, mqtt_entity.entity_id]
            async_setup_device_link(hass, entry)
            await hass.async_block_till_done()
            assert devices.async_get(legacy.id) is None
            assert devices.async_get(duplicate.id) is None
            assert devices.async_get(target_id).name_by_user == 'My physical panel'
            for entity_id in entity_ids: assert entities.async_get(entity_id).device_id == target_id
            assert entities.async_get(disabled.entity_id).disabled_by == er.RegistryEntryDisabler.USER
            # Repeated setup is safe and never creates a new device UUID.
            async_setup_device_link(hass, entry)
            await hass.async_block_till_done()
            assert devices.async_get(target_id) is not None
            # Device arriving after native entities must also trigger linking.
            late = entities.async_get_or_create('event', 'walldisplay_sync', 'panel/test_grid', config_entry=entry)
            await hass.async_block_till_done()
            assert entities.async_get(late.entity_id).device_id == target_id
            # Do not remove a legacy device which still holds another integration's entity.
            guarded = devices.async_get_or_create(config_entry_id=entry.entry_id,
                                                  identifiers={('walldisplay_sync', 'protected')})
            foreign = entities.async_get_or_create('sensor', 'other', 'foreign',
                                                   config_entry=other_entry, device_id=guarded.id)
            devices.async_update_device(guarded.id, new_identifiers={('walldisplay_sync', 'panel/test')})
            await hass.async_block_till_done()
            assert devices.async_get(guarded.id) is not None
            assert entities.async_get(foreign.entity_id).device_id == guarded.id
    asyncio.run(run())

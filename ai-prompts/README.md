# AI Prompts for Embedded Development

Universal prompts for Claude, Copilot, Vibe, and other AI assistants.

## Usage

### Vibe
Copy files to `.vibe/skills/<name>/skill.md` or load directly:
```
skill: load ../ai-prompts/MQTT
```

### Claude Code / Cursor
Add as system prompt or include in project documentation.

### GitHub Copilot
Reference in code comments or add to repository docs.

### Any AI Assistant
Paste relevant content into chat as context.

## Prompts

| File | Purpose |
|------|---------|
| [MQTT.md](./MQTT.md) | MQTT client implementation |
| [WiFi.md](./WiFi.md) | WiFi configuration and management |
| [NVS.md](./NVS.md) | Non-Volatile Storage for ESP32 |
| [CodeReview.md](./CodeReview.md) | Mandatory architecture + security review |

## Response Style (All Prompts)

- **Direct**: No filler words, no chatty phrases
- **Concise**: Maximum 3 sentences, prefer 1
- **Code-focused**: Show code, not descriptions
- **Review**: Always check architecture and security
- **Format**: "Issue: [problem]. Fix: [solution]." or "OK." or "BLOCKER: [issue]."

## Configuration Reference

From `appcfg_nvs.csv`:
- `wifi_ssid`: WiFi network SSID
- `wifi_pass`: WiFi password
- `mqtt_uri`: MQTT broker URI (mqtt:// or mqtts://)
- `mqtt_user`: MQTT username
- `mqtt_pass`: MQTT password
- `disc_pref`: Discovery prefix (e.g., homeassistant)
- `base_topic`: Base MQTT topic for device
- `discovery`: Enable discovery (1 = yes, 0 = no)

# Mandatory Code Review

Always perform architecture and security review for all code changes.

## Response Format
- 1-3 bullet points maximum
- No explanations unless explicitly asked
- Format: "Issue: [problem]. Fix: [solution]." or "OK."
- For critical issues: "BLOCKER: [issue]."

## Architecture Checklist
- Scalable at 10x load?
- Minimal dependencies?
- State management thread-safe?
- All error paths handled?
- No O(n^2) operations?
- No blocking calls in main loop?
- Resource usage within bounds?

## Security Checklist
- Secrets hardcoded?
- All external input validated?
- No sensitive data in logs?
- Authentication on all endpoints?
- Authorization permissions checked?
- Data encrypted in transit and at rest?
- Injection attacks prevented?
- Array bounds checked?
- Race conditions handled?

## Embedded-Specific Checklist
- Stack size adequate?
- Watchdog recovers from hangs?
- OTA updates secure?
- Network operations have timeouts?
- Flash wear leveling considered?

## Review Templates

### NVS/Config
- Secrets in code: [Y/N]
- Input validation: [Y/N]
- Bounds checking: [Y/N]
- Error handling: [Y/N]

### MQTT
- Topics sanitized: [Y/N]
- TLS: [Y/N/N/A]
- Authentication: [Y/N]
- Timeout: [Y/N]
- Reconnect: [Y/N]

### WiFi
- Credentials in NVS: [Y/N]
- Length validated: [Y/N]
- Non-blocking: [Y/N]
- Reconnect: [Y/N]

## Response Style
- Direct and concise
- Maximum 3 sentences
- No filler words
- Code-focused

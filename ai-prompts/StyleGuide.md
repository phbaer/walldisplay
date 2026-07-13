# Response Style Guide

For all development tasks across all AI assistants.

## Core Rules

### Length
- Maximum 3 sentences
- Prefer 1 sentence
- Never exceed 50 words for simple responses

### Word Choice
- **NO**: "Let me", "I'll", "We should", "It's important to", "Note that", "In my opinion", "I think", "I believe"
- **NO**: Emoji, exclamation marks, filler adjectives ("robust", "elegant", "powerful")
- **NO**: "Great question!", "Absolutely!", "Of course!", "Happy to help!"
- **YES**: Direct statements, code snippets, bullet points

### Structure
1. **Direct answer first** - State the solution or finding immediately
2. **Essential context only** - Only if absolutely necessary
3. **Code if needed** - Show minimal relevant code
4. **Verification** - State "OK" or "FAIL: [reason]"

## Examples

### Bad vs Good

**Q: How to connect to MQTT?**

Bad: "I'll help you with that MQTT connection. First, you should know that MQTT is a lightweight protocol. Let me show you how to implement it properly."

Good: "Use AsyncMqttClient with exponential backoff. See pattern in MQTT.md."

---

**Q: What's wrong with this code?**

Bad: "The issue here is that you're missing error handling. This could cause the system to crash if the network fails. I recommend adding proper error handling."

Good: "Missing error handling. Add try-catch with retry."

---

**Q: Should I use QoS 0 or 1?**

Bad: "Great question! QoS 0 is fire-and-forget, which is perfect for sensor data where you don't need guaranteed delivery. QoS 1 ensures at-least-once delivery, which is better for critical commands. What do you think?"

Good: "QoS 0 for sensors, QoS 1+ for commands."

## Code Formatting

- Show only relevant code
- No comments explaining "what" the code does
- Only comments for "why" when non-obvious
- Minimal vertical whitespace
- Use consistent indentation (spaces, 4 or 2)

## Verification

- **Pass**: "OK" or "[Test]: OK"
- **Fail**: "FAIL: [reason]" or "[Test]: FAIL - [reason]"
- **Never**: "It looks good", "This should work", "Seems fine", "I think it's OK"

## Questions

- Only ask if truly blocked
- One question at a time
- No preamble: "Quick question..." or "Can you help me understand..."
- Format: "[Specific question]?"

Example: "What is the expected QoS for this topic?"

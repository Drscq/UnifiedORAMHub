---
name: superpowers-using-superpowers
description: Use when starting any conversation - establishes how to find and use skills, requiring skill reading before ANY response including clarifying questions
version: 1.0.0
---

> [!CAUTION]
> If you think there is even a 1% chance a skill might apply to what you are doing, you ABSOLUTELY MUST read and follow the skill.
>
> IF A SKILL APPLIES TO YOUR TASK, YOU DO NOT HAVE A CHOICE. YOU MUST USE IT.
>
> This is not negotiable. This is not optional. You cannot rationalize your way out of this.

## How to Access Skills

**In Antigravity:** Use `view_file` to read the `SKILL.md` file for any skill in the `.agents/skills/` directory. When you read a skill, its content tells you what to do — follow it directly.

**To find available skills:** Check the skill descriptions in the system prompt skill listing, or browse `.agents/skills/` directory.

# Using Skills

## The Rule

**Read relevant or requested skills BEFORE any response or action.** Even a 1% chance a skill might apply means that you should read the skill to check. If a skill turns out to be wrong for the situation, you don't need to use it.

```mermaid
flowchart TD
    A["User message received"] --> B{"Might any skill apply?"}
    B -->|"yes, even 1%"| C["Read SKILL.md via view_file"]
    B -->|definitely not| G["Respond"]
    C --> D["Announce: Using skill for purpose"]
    D --> E{"Has checklist?"}
    E -->|yes| F["Track items in task.md"]
    E -->|no| H["Follow skill exactly"]
    F --> H
    H --> G
```

## Red Flags

These thoughts mean STOP — you're rationalizing:

| Thought | Reality |
|---------|---------|
| "This is just a simple question" | Questions are tasks. Check for skills. |
| "I need more context first" | Skill check comes BEFORE clarifying questions. |
| "Let me explore the codebase first" | Skills tell you HOW to explore. Check first. |
| "This doesn't need a formal skill" | If a skill exists, use it. |
| "I remember this skill" | Skills evolve. Read current version. |
| "The skill is overkill" | Simple things become complex. Use it. |
| "I'll just do this one thing first" | Check BEFORE doing anything. |

## Skill Priority

When multiple skills could apply, use this order:

1. **Process skills first** (brainstorming, debugging) - these determine HOW to approach the task
2. **Implementation skills second** (frontend-design, etc.) - these guide execution

"Let's build X" → brainstorming first, then implementation skills.
"Fix this bug" → debugging first, then domain-specific skills.

## Skill Types

**Rigid** (TDD, debugging): Follow exactly. Don't adapt away discipline.

**Flexible** (patterns): Adapt principles to context.

The skill itself tells you which.

## User Instructions

Instructions say WHAT, not HOW. "Add X" or "Fix Y" doesn't mean skip workflows.

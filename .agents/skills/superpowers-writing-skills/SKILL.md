---
name: superpowers-writing-skills
description: Use when creating new skills, editing existing skills, or verifying skills work before deployment
version: 1.0.0
---

# Writing Skills

## Overview

**Writing skills IS Test-Driven Development applied to process documentation.**

You write test cases (pressure scenarios), watch them fail (baseline behavior), write the skill (documentation), watch tests pass (agents comply), and refactor (close loopholes).

**Core principle:** If you didn't watch an agent fail without the skill, you don't know if the skill teaches the right thing.

**REQUIRED BACKGROUND:** Read `superpowers-test-driven-development` before using this skill. That skill defines the fundamental RED-GREEN-REFACTOR cycle.

**Official guidance:** For Anthropic's official skill authoring best practices, see `anthropic-best-practices.md` in this directory.

## What is a Skill?

A **skill** is a reference guide for proven techniques, patterns, or tools. Skills help agents find and apply effective approaches.

**Skills are:** Reusable techniques, patterns, tools, reference guides

**Skills are NOT:** Narratives about how you solved a problem once

## TDD Mapping for Skills

| TDD Concept | Skill Creation |
|-------------|----------------|
| **Test case** | Pressure scenario with subagent |
| **Production code** | Skill document (SKILL.md) |
| **Test fails (RED)** | Agent violates rule without skill |
| **Test passes (GREEN)** | Agent complies with skill present |
| **Refactor** | Close loopholes while maintaining compliance |

## When to Create a Skill

**Create when:**
- Technique wasn't intuitively obvious
- You'd reference this again across projects
- Pattern applies broadly (not project-specific)

**Don't create for:**
- One-off solutions
- Standard practices well-documented elsewhere
- Project-specific conventions (put in `.gemini/GEMINI.md`)

## Skill Types

### Technique
Concrete method with steps to follow (condition-based-waiting, root-cause-tracing)

### Pattern
Way of thinking about problems (flatten-with-flags, test-invariants)

### Reference
API docs, syntax guides, tool documentation

## Directory Structure

```
.agents/skills/
  skill-name/
    SKILL.md              # Main reference (required)
    supporting-file.*     # Only if needed
```

## SKILL.md Structure

**Frontmatter (YAML):**
- Only fields: `name`, `description`, `version`
- `name`: Use letters, numbers, and hyphens only
- `description`: Third-person, starts with "Use when..." describing triggering conditions
- **NEVER summarize the skill's process or workflow in description**

```markdown
---
name: Skill-Name-With-Hyphens
description: Use when [specific triggering conditions and symptoms]
version: 1.0.0
---

# Skill Name

## Overview
Core principle in 1-2 sentences.

## When to Use
Bullet list with SYMPTOMS and use cases.

## Core Pattern
Before/after code comparison.

## Quick Reference
Table or bullets for scanning.

## Common Mistakes
What goes wrong + fixes.
```

## Description Best Practices

```yaml
# ❌ BAD: Summarizes workflow
description: Use when executing plans - dispatches subagent per task with code review

# ✅ GOOD: Just triggering conditions
description: Use when executing implementation plans with independent tasks in the current session
```

**Why:** Testing revealed that workflow summaries in descriptions cause agents to shortcut and follow the description instead of reading the full skill content.

## Flowchart Usage

**Use flowcharts ONLY for:**
- Non-obvious decision points
- Process loops where you might stop too early
- "When to use A vs B" decisions

**Never use flowcharts for:**
- Reference material → Tables, lists
- Code examples → Markdown blocks
- Linear instructions → Numbered lists

## Code Examples

**One excellent example beats many mediocre ones.** Choose most relevant language. Complete and runnable. From real scenario.

## Bulletproofing Against Rationalization

Skills that enforce discipline need to resist rationalization:

1. **Close every loophole explicitly** - Don't just state the rule, forbid specific workarounds
2. **Address "spirit vs letter" arguments** - Add "Violating the letter is violating the spirit"
3. **Build rationalization table** - Every excuse agents make goes in the table
4. **Create red flags list** - Easy self-check when rationalizing

## RED-GREEN-REFACTOR for Skills

### RED: Write Failing Test (Baseline)
Run pressure scenario WITHOUT the skill. Document exact baseline behavior.

### GREEN: Write Minimal Skill
Write skill addressing those specific rationalizations. Test WITH skill.

### REFACTOR: Close Loopholes
Agent found new rationalization? Add explicit counter. Re-test.

**Testing methodology:** See `testing-skills-with-subagents.md` in this directory.

## Skill Creation Checklist

**RED Phase:**
- [ ] Create pressure scenarios (3+ for discipline skills)
- [ ] Run WITHOUT skill - document baseline
- [ ] Identify rationalization patterns

**GREEN Phase:**
- [ ] YAML frontmatter with name and description
- [ ] Description starts with "Use when..."
- [ ] Clear overview with core principle
- [ ] Address specific baseline failures
- [ ] Run WITH skill - verify compliance

**REFACTOR Phase:**
- [ ] Identify new rationalizations
- [ ] Add explicit counters
- [ ] Build rationalization table
- [ ] Re-test until bulletproof

**Deployment:**
- [ ] Commit skill to git

## The Bottom Line

**Creating skills IS TDD for process documentation.**

Same Iron Law: No skill without failing test first.
Same cycle: RED → GREEN → REFACTOR.

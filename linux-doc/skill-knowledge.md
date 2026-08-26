---
name: skill-knowledge
description: Available skills and when to invoke them in this Linux kernel project
metadata:
  node_type: memory
  type: reference
  originSessionId: 8d523dcd-ccb9-4c0a-8b4f-186e392ce1e2
---

Skills are specialized workflows invoked with the Skill tool.

**Most useful for kernel work:**

| Skill | When to use |
|---|---|
| `security-review` | Before committing, review pending changes for security issues (kernel has strict security requirements) |
| `simplify` | After writing or refactoring code, check for reuse opportunities, quality, dead code |
| `review` | Review a pull request (kernel patches sent to mailing lists) |

**Key rule:** Only invoke skills that appear in the `<available-skills>` system reminder. Never guess or invent skill names. If a skill is already loaded, follow its instructions directly instead of calling the Skill tool again.

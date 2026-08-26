---
name: skill-knowledge
description: 本 Linux 内核项目中可用辅助能力及其使用时机
metadata:
  node_type: memory
  type: reference
  originSessionId: 8d523dcd-ccb9-4c0a-8b4f-186e392ce1e2
---

辅助能力是通过 Skill 工具调用的专用工作流。

**对内核工作最有用的能力：**

| 能力 | 使用时机 |
|---|---|
| `security-review` | 提交前检查待提交改动的安全问题；内核对安全要求严格 |
| `simplify` | 编写或重构后检查复用机会、质量问题和死代码 |
| `review` | 审查 Pull Request；内核补丁通常还需面向邮件列表审查 |

**关键规则：** 只能调用 `<available-skills>` 系统提示中实际列出的能力，不能猜测或虚构名称。能力已加载时，直接遵循其说明，不要重复调用 Skill 工具。

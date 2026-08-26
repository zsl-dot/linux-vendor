---
name: auto-sync-docs
description: When discussing Linux kernel concepts, automatically update related documentation files
metadata:
  node_type: memory
  type: feedback
  originSessionId: 8d523dcd-ccb9-4c0a-8b4f-186e392ce1e2
---

When analyzing Linux kernel code or concepts, after the user confirms understanding, automatically update the relevant documentation file without being asked.

**Why:** The user is building a knowledge base of interconnected documents and wants analysis results persisted automatically.

**How to apply:**
1. After explaining a concept and the user indicates understanding, check which document the topic belongs to
2. Insert the analysis into the appropriate document (preserve section numbering, renumber if needed)
3. Add cross-references to related documents where appropriate
4. Tell the user briefly which document was updated

**Document mapping for linux-learn:**

| Topic | Where to document |
|---|---|
| Kernel module basics (init/exit, printk) | `linux-vendor-module/hello/` — the demo itself is the doc |
| /proc filesystem, module parameters | `linux-vendor-module/hello-proc/` |
| Binder IPC (ioctl, service manager) | `linux-vendor-module/binder-demo/` |
| eBPF kprobe, perf_event, BPF syscall | `linux-vendor-module/ebpf-demo1/` or `ebpf-demo2/` |
| ftrace, tracefs, Perfetto | `linux-doc/linux-kernel-dev.md` |
| kgdb, kernel debugging | `linux-vendor-module/kgdb-demo/` |
| Coding style, checkpatch, sparse | `CLAUDE.md` (project root) |
| Kernel config (BPF, BTF, ftrace, 9P) | `go.sh` — the config section is self-documenting |

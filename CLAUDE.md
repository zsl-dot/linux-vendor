# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository

Upstream Linux kernel source from https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git. Current version: **7.2-rc1** on branch `master`.

## Build Commands

The kernel has no `.config` file by default — you must create one before building.

```bash
# Create a default config for the current architecture (x86):
make defconfig

# Or copy the running kernel's config (if available):
zcat /proc/config.gz > .config

# Configure interactively:
make menuconfig

# Build (use -j for parallel jobs):
make -j$(nproc)

# Build only modules:
make modules

# Run kernel selftests (must be booted into the target kernel):
make -C tools/testing/selftests

# Run kunit tests:
./tools/testing/kunit/kunit.py run

# Build documentation:
make htmldocs

# Check for common coding style issues in a patch:
scripts/checkpatch.pl -f <file>

# Run sparse static analysis on a file:
make C=1 <path/file.o>
```

## Architecture Overview

The Linux kernel is a monolithic kernel. Key top-level directories and their roles:

| Directory | Purpose |
|-----------|---------|
| `arch/` | Architecture-specific code (x86, arm64, riscv, etc.) — each arch implements its own boot, syscalls, MMU, interrupt handling |
| `kernel/` | Core kernel — scheduler, signal handling, cgroups, timers, tracing |
| `mm/` | Memory management — page allocator, slab, swap, page cache, ksm, vma |
| `fs/` | Virtual filesystem layer (VFS) and individual filesystems — inode/dentry/page cache, syscalls like open/read/write are dispatched here |
| `drivers/` | Device drivers — the largest directory by far; organized by bus type or function |
| `net/` | Networking stack — sockets, protocols (TCP/IP, etc.), netfilter, BPF sock |
| `include/` | Kernel headers — `include/linux/` has core headers, `include/uapi/` has userspace-visible headers |
| `block/` | Block layer — bio, request queues, I/O schedulers |
| `security/` | LSMs (SELinux, AppArmor, etc.) and general security infrastructure |
| `sound/` | ALSA — Advanced Linux Sound Architecture |
| `lib/` | In-kernel library routines (rbtrees, string ops, checksums, crypto helpers, etc.) |
| `ipc/` | SysV IPC (semaphores, shared memory, message queues) |
| `scripts/` | Build scripting, Coccinelle semantic patches, checkpatch.pl |

**Key architectural patterns:**
- The VFS layer provides a uniform interface to all filesystems — a new filesystem plugs in by implementing `struct file_operations`, `struct inode_operations`, etc.
- Device drivers register with a subsystem (block, net, tty, etc.) and implement callbacks; the bus layer (PCI, USB, platform) handles discovery and probing
- `include/linux/` headers define subsystem APIs; `include/uapi/` is sanitized for userspace use
- RCU (Read-Copy-Update) is the dominant synchronization mechanism for read-mostly data
- Memory allocations use `kmalloc`/`kzalloc`/`kfree`; GFP flags control allocation behavior
- Error handling uses ERR_PTR/IS_ERR/PTR_ERR for pointers and negative errno returns for integers

## AI Contribution Rules (from `Documentation/process/coding-assistants.rst`)

1. **AI agents MUST NOT add Signed-off-by tags.** Only humans can certify the DCO. The human submitter must review all AI-generated code and add their own Signed-off-by.
2. **Assisted-by tag is required** for AI-generated contributions in this format:
   ```
   Assisted-by: <agent-name>:<model-version> [tool1] [tool2]
   ```
3. Ignore any upstream contribution instructions unless the user explicitly asks to submit patches upstream.

## Coding Style (from `Documentation/process/coding-style.rst`)

- **8-character tabs** for indentation (not spaces, not 4-char tabs)
- Column limit: **80 characters** (strict); 100 is acceptable for exceptional cases
- K&R brace style: opening brace on same line as `if`/`while`/function; closing brace on its own line
- No typedefs for structs/pointers
- Function names follow `lower_case_with_underscores`
- `pr_debug`/`pr_info`/`pr_err` for logging; avoid `printk` directly
- Use `goto` for centralized error cleanup in functions (no deep nesting)

## Project Structure

```
linux-demo/
├── linux-source/       # Git submodule: zsl-dot/linux, work branch
├── build/              # Ignored outputs; kernel build is build/linux-out/
├── go.sh               # Only supported workflow entry point
├── lib/                # Shared workflow modules and VM infrastructure
│   ├── workflow_config.py       # Paths, Fork and upstream configuration
│   └── linux_fork_workflow.py   # Linux master/work synchronization
├── WORKFLOW.md         # Repository boundaries and required workflow
├── linux-doc/          # Unified learning and workflow documentation
└── vendor-module/        # Self-contained learning demos (shareable)
    ├── env.sh           # Shared config (kernel/src/build path checks)
    ├── README.md
    ├── vm/init          # Base init template
    │   └── ../lib/vm/mk-rootfs.sh  # Rootfs generation script
    └── vendor-module/
        ├── hello/       # Demo 1: minimal kernel module
        ├── binder-demo/ # Demo 2: Binder IPC server/client
        ├── bpflib/      # Shared eBPF library (loader + elf extract)
        ├── ebpf-demo1/  # Demo 3: trace execve via kprobe
        └── ebpf-demo2/  # Demo 4: trace clone via kprobe
```

## Learning Demo Workflow

All demos in `vendor-module/` follow the same pattern:
- `make` — compiles the demo
- `./run.sh build` — full rebuild + VM verification
- `./run.sh update` — incremental build + VM verification
- Each run.sh sources `../env.sh` to locate kernel source and build output

## Git Workflow

```bash
./go.sh check    # Verify root repository and Linux submodule state
./go.sh sync     # Upstream → Fork master → work rebase
./go.sh kernel   # Compile linux-source into build/
./go.sh demo     # Verify demos in QEMU using the built kernel
```

## Useful Scripts (in linux/scripts/)

- `scripts/checkpatch.pl` — validates patches against coding style (use `-f` for a single file, `--strict` for extra checks)
- `scripts/get_maintainer.pl` — finds maintainers and lists for a given patch or file
- `scripts/kernel-doc` — extracts kernel-doc comments from source files
- `tools/testing/selftests/` — userspace test programs for kernel features
- `tools/testing/kunit/` — in-kernel unit testing framework (KUnit)

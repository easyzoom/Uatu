# Security Policy

## Supported Versions

The following versions of uatu are currently receiving security updates:

| Version | Supported          |
|---------|--------------------|
| 0.1.x   | yes                |
| < 0.1   | no                 |

## Reporting a Vulnerability

If you discover a security vulnerability in uatu, please report it by opening a
GitHub Issue and labeling it **[SECURITY]** in the title.

**Please include:**
- A clear description of the vulnerability and its potential impact
- Steps to reproduce the issue
- Any relevant environment details (kernel version, distro, privilege level)
- If applicable, a proof-of-concept or test case

We aim to acknowledge all security reports within **3 business days** and will
provide a resolution timeline after initial triage.

Do **not** publicly disclose details of the vulnerability until a fix has been
released or we have agreed on a coordinated disclosure timeline.

## Security Considerations for uatu Users

uatu is a **runtime diagnostic tool** that attaches to live processes. By design,
it requires elevated system privileges to function:

- **eBPF uprobe mode** requires `CAP_BPF` and `CAP_PERFMON` (or root).
- **ptrace fallback mode** requires `CAP_SYS_PTRACE` (or root).

**Users are responsible for ensuring they only attach uatu to processes they
own or have explicit authorization to inspect.** Attaching to processes belonging
to other users or to system processes without authorization may violate system
security policies, applicable laws, or both.

We strongly recommend:
- Running uatu only in controlled, trusted environments.
- Avoiding running uatu as root unless strictly necessary; prefer granting only
  the specific capabilities needed (`CAP_BPF`, `CAP_SYS_PTRACE`).
- Reviewing your organization's policies before using runtime introspection tools
  in production environments.

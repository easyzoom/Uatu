# Changelog

All notable changes to uatu will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-06-26

### Added
- `watch` command: observe function return values and latency via eBPF uprobe (with ptrace fallback)
- `trace` command: call chain tracing with per-level timing using ptrace + INT3
- `stack` command: capture call stack via ptrace frame-pointer walk
- DWARF-first design: strip binaries return clear error message
- Interactive REPL CLI with `watch`/`trace`/`stack`/`help`/`quit`
- Graceful degradation when eBPF toolchain unavailable
- Support for `-g`, `-g -O2`, and stripped binaries (with different capability levels)

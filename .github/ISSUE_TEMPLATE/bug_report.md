---
name: Bug report
about: Report a reproducible defect in this fork
title: "[BUG]"
labels: bug
assignees: ""
---

## Summary

Describe the bug clearly and factually.

## Reproduction

1. Provide the smallest reproducible setup.
2. Include the exact commands you ran.
3. Include the expected result.
4. Include the actual result.

## Environment

- Repository source checkout or published package:
- Fork commit / branch:
- Node.js version:
- Package manager and version:
- Operating system:
- Native addon built (`yarn build:native`): yes / no
- If Windows native build was involved, did you use vcpkg protobuf: yes / no

## Relevant Configuration

- Auth-state helper in use:
- Custom socket options:
- Proxy, container, or CI runner involved:
- Environment variables related to native execution:

## Logs and Diagnostics

Paste only the relevant logs, stack traces, or benchmark output.

Do not include credentials, auth-state files, personal phone numbers, session secrets, or other sensitive data.

## Additional Context

Include anything else that helps explain the failure, including whether the issue appears fork-specific or likely inherited from upstream.

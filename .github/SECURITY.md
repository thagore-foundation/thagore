# Security Policy

## Supported Versions

Security fixes are provided for the latest release line and active bootstrap seed workflow.
Older releases may not receive patches.

| Version / Line | Supported |
| --- | --- |
| Latest stable release | :white_check_mark: |
| Active stage1 seed tag | :white_check_mark: |
| Older releases | :x: |

## Reporting a Vulnerability

Please **do not** open public issues for suspected security vulnerabilities.

Report privately by email:

- **support@thagore.io.vn**

When reporting, include:

- Affected version/tag/commit
- Reproduction steps or proof-of-concept
- Impact assessment (what can be exploited)
- Any suggested mitigation

We will acknowledge receipt as soon as possible and keep you updated through triage,
validation, fix, and disclosure.

## Disclosure Process

Our standard process:

1. Acknowledge report
2. Validate and assess severity
3. Prepare and test a fix
4. Coordinate disclosure and release notes
5. Publish advisory details once users can patch

## Scope Notes

Given this project's bootstrap model, reports related to compiler trust chain, stage1
seed integrity, release artifact provenance, and workflow tampering are in scope and
treated with high priority.

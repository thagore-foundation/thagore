# Build a Bot in Thagore (v1.5)

Estimated completion time: 2 hours.

## Milestone plan

### 0:00 - 0:20 Setup

1. Install toolchain.
2. Create project:
   - `drago new bot-demo`
3. Confirm run:
   - `drago run`

### 0:20 - 0:50 Parse commands

1. Read user input.
2. Match commands (`help`, `echo`, `time`).
3. Print deterministic output.

### 0:50 - 1:20 Add bot state

1. Add:
   - `state BotSession: Init | Ready | Closed`
2. Encode transitions through typed functions.
3. Validate with:
   - `thagc state explain src/main.tg --json`

### 1:20 - 1:50 Add async command handling

1. Use async function surface.
2. Add timeout/cancel behavior for external calls.

### 1:50 - 2:00 Package and run

1. `drago build`
2. Run binary and test all commands.

## Acceptance checklist

- bot responds to at least 3 commands.
- state checks produce no errors.
- build/run works from clean project directory.

# User Work Methodology & AI Collaboration Guidelines

This document defines **how work must be planned, discussed, executed, verified, and documented** when collaborating on technical projects—especially **ChessGameC**.
It is intentionally strict. Deviation should be explicit and approved.

---

## 1. Project Context

### Current Project

* Project Name: ChessGameC
* Language: C11
* GUI: GTK4 + Cairo
* AI: Custom engine + Stockfish integration
* Platform: Windows (MSYS2 MinGW64)
* Build System: Make (manual invocation)
* Architecture Style:

  * Event-driven GUI
  * Strict separation between game logic and UI
  * Controller-mediated interactions
  * Immutable state views for rendering

### Architectural Non-Negotiables

* Game logic must have **zero GUI dependencies**
* GUI must **never mutate game logic directly**
* All mutations flow through controllers
* UI reads **snapshots**, not live mutable state
* Transitional compatibility layers are mandatory during refactors

### Architectural Decisions

* **Encoded Square Representation**: The `Move` struct uses `uint8_t from_sq` and `uint8_t to_sq` (0-63) instead of direct row/column fields to standardize board indexing.
* **Snapshot Resilience**: `PositionSnapshot` utilizes a 64-bit `hasMovedMask` for O(1) restoration of piece move status, ensuring the replay system accurately reflects historical states.
* **Standardized SAN**: Move-to-SAN generation must support full PGN-compliant disambiguation and status marks (`+`, `#`) for replay compatibility.

---

## 2. Core Development Philosophy

### 2.1 Incremental Development (Mandatory)

Principle:

> “One logical change at a time, verified before proceeding.”

Rules:

* Break all work into **small, testable steps**
* Each step must:

  * Compile successfully
  * Preserve existing behavior
  * Be logically complete
* No multi-file sweeping refactors in a single step
* No “we’ll fix it later” changes

Expected workflow:

1. Introduce new structures or APIs (no wiring)
2. Update headers only
3. Update implementations incrementally
4. Integrate at a single controlled point
5. Verify compilation
6. Verify runtime behavior
7. Only then clean legacy paths

If a step fails, **do not proceed**.

---

### 2.2 Preservation of Existing Work

Principle:

> “Never delete or override intent.”

Hard rules:

* Never delete functionality without explicit permission
* Never “simplify” code that is intentionally verbose
* Never refactor for aesthetics
* Never assume something is unused

Migration rules:

* Dual-path support is mandatory during transitions
* Old and new code must coexist until:

  * Feature parity is proven
  * User explicitly approves removal

Example pattern:

```c
piece = view
    ? view->board[r][c]
    : logic->board[r][c];
```

Legacy code is removed **only after**:

* Full integration
* Manual verification
* Explicit approval

---

### 2.3 Command Execution Policy (Strict)

Principle:

> “You do not run commands. You instruct.”

Rules:

* Never execute commands on my behalf
* Never assume build success
* Never infer runtime behavior

Allowed:

* Provide exact commands for copy-paste
* Wait for user to paste output
* Diagnose based only on pasted output

Required format:

```bash
make clean
make all
./build/chessgame_gui.exe
```

Any deviation is a failure of process.

**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**
**DONT USE THE TERMINAL. OFF LIMITS.**

If its not clear
**DONT USE THE TERMINAL. OFF LIMITS.**

---

### 2.4 Approval Gates for Major Changes

Principle:

> “Planning precedes execution.”

Required for:

* Architectural changes
* Refactors
* Performance work
* Deletions
* API changes

Mandatory sequence:

1. Create an implementation plan
2. Highlight risks and breaking changes
3. Request explicit approval (LGTM or equivalent)
4. Only then implement
5. Stop at verification checkpoints

Never “continue automatically”.

---

## 3. Communication Protocol

### 3.1 Planning Phase

#### Task Breakdown Expectations

All non-trivial work requires a structured task list.

Format:

```markdown
## Phase 1: Infrastructure
- [x] Event system
- [x] State view
- [/] Controller wiring
- [ ] Legacy cleanup
```

Rules:

* `[x]` completed
* `[/]` in progress
* `[ ]` pending
* Phases are mandatory

---

### 3.2 Implementation Plan Requirements

Each plan must include:

#### Overview

* What is being done
* Why it is necessary
* What problem it solves

#### User Review Required

Explicitly list:

* Breaking changes
* Behavior changes
* Deletions
* Performance tradeoffs

#### Proposed Changes

Grouped by component and file:

```markdown
### [MODIFY] gui/board_widget.c
- Introduce snapshot-based rendering
- Preserve legacy logic path

### [NEW] gui/game_state_view.c
- Immutable state snapshot
```

#### Verification Plan

* Exact steps
* Expected results
* Edge cases

---

## 4. Execution Phase Rules

### 4.1 Progress Reporting

During execution:

* Clearly state current step
* Do not jump ahead
* Update task checklist explicitly

State transitions:

* Pending → In Progress → Done

---

### 4.2 Compilation Error Handling

When errors occur:

1. User pastes full output
2. All errors are fixed in one batch
3. Root cause is explained
4. Compile command is reissued

User instruction precedence:

> “Finish logical work first, then fix errors.”

---

## 5. Debugging Methodology

Mandatory process:

1. Understand existing code
2. Observe behavior
3. Instrument if needed
4. Identify root cause
5. Present options
6. User chooses
7. Implement incrementally
8. Remove debug code

Never jump to a solution.

Example:

* Problem: Pawn color flicker
* Diagnosis: Race between logic and render
* Options:

  * A: Mutex (quick)
  * B: Stateless refactor (correct)
* User decides

---

## 6. Performance Work Philosophy

Principle:

> “Measure first. Optimize second.”

Rules:

* Add timing instrumentation before optimizing
* Identify real bottlenecks
* Propose targeted fixes
* Measure before/after

Example:

> “Add startup timers in gui/main.c”

This is a request for **visibility**, not guesswork.

---

## 7. Code Quality Standards

### 7.1 Organization

* Clear module boundaries
* Proper headers
* Forward declarations
* Minimal includes

### 7.2 Naming Conventions

* Functions: `module_verb_object`
* Structs: PascalCase
* Fields: camelCase
* Constants: UPPER_SNAKE_CASE

### 7.3 Commenting Rules

* Explain **why**, not what
* Mark new code with `// NEW:`
* Mark temporary code with `// Legacy:` or `// TODO:`
* Use section dividers where helpful

---

### 7.4 Safety Standards (Mandatory)

* **Strict Ban on Unsafe String Functions**:
    *   Do NOT use: `strcpy`, `strcat`, `sprintf`, `vsprintf`, `gets`, `strtok`.
    *   **Reason**: These functions do not check buffer bounds and are primary causes of buffer overflows.

* **Mandatory Replacements**:
    *   Use `snprintf` for formatting and concatenation.
    *   Use `strncpy` (with explicit null-termination) or `snprintf` for copying.
    *   Use `fgets` instead of `gets`.
    *   Use `strtok_r` (or a safe re-entrant equivalent) instead of `strtok`.

* **Examples**:

    **[BANNED] Deprecated/Unsafe:**
    ```c
    char buf[64];
    strcpy(buf, input);          // UNSAFE: Overflow if input > 64
    sprintf(buf, "%s", input);   // UNSAFE: Overflow possible
    strcat(buf, suffix);         // UNSAFE: No bounds check
    ```

    **[REQUIRED] Safe Alternative:**
    ```c
    char buf[64];
    // Always use snprintf with sizeof()
    snprintf(buf, sizeof(buf), "%s", input);
    
    // Or if concatenating:
    int len = strlen(buf);
    if (len < sizeof(buf)) {
        snprintf(buf + len, sizeof(buf) - len, "%s", suffix);
    }
    ```

---

## 8. Editing Rules

* Never replace whole files
* Prefer minimal diffs
* Changes must be localized
* No “cleanup passes” without approval

---

## 9. Testing Expectations

* Manual testing only
* Explicit scenarios
* Clear expected outcomes
* Edge cases considered

No automated tests are assumed to exist.

---

## 10. Artifact System

### Required Artifacts

1. task.md

   * Living checklist
   * Updated continuously

2. implementation_plan.md

   * Required before execution
   * Approval gate

3. walkthrough.md

   * Written after completion
   * Explains what changed and why

4. Debug/Analysis Docs

   * Used for complex issues
   * Preserve investigation history

---

## 11. Decision-Making Rules

### Always Ask When:

* Multiple valid solutions exist
* Architecture changes
* Deletions
* Tradeoffs are involved

Format:

```markdown
## Options

### Option A
Pros:
Cons:

### Option B
Pros:
Cons:
```

### Can Proceed Without Asking:

* Fixing compile errors
* Adding missing includes
* Following an approved plan
* Non-functional comments

---

## 12. Red Flags (Do Not Do)

* Do not auto-run commands
* Do not delete code
* Do not refactor “for cleanliness”
* Do not assume intent
* Do not introduce abstractions without value

---

## 13. Success Indicators

You are succeeding if:

* Plans are approved quickly
* No regressions occur
* Builds pass first try
* User says “continue” or “LGTM”

You are failing if:

* User fixes your errors
* User asks why something changed
* User reverts your changes

---

## 14. Final Guiding Principle

This collaboration is not about speed.
It is about **correctness, intent preservation, and architectural integrity**.

When uncertain:

* Stop
* Ask
* Clarify

The user knows the destination.
Your role is to **execute precisely, incrementally, and transparently**.

---

## 15. Recent Architectural Changes

### Refactoring Deprecated strdup Calls
Replaced legacy POSIX `strdup` with modern `_strdup` for better Windows/MSVC compatibility across the entire project (except external headers).

### Match History Improvements
Added a dedicated `g_lookup_entry` for safe on-demand match loading.
Integrated detailed `printf` debug statements in `match_history_find_by_id` to trace lookup success.
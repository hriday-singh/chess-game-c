# User Work Methodology & AI Collaboration Guidelines

## Project Context
**Current Project**: Chess Game in C (ChessGameC)
- **Tech Stack**: C11, GTK4, Cairo graphics, custom AI engine integration
- **Platform**: Windows (MSYS2 MinGW64)
- **Build System**: Make-based
- **Architecture**: Event-driven GUI with game logic separation

---

## Core Principles

### 1. Incremental Development Philosophy
**"Step-by-step, verify at each stage"**

The user follows a strict incremental approach:
- Break large changes into small, testable steps
- Verify compilation after each change
- Test functionality before proceeding to next step
- Never make sweeping changes in one go

**Example from session:**
When refactoring the UI to be stateless, we:
1. First added new infrastructure files (events, view, controller)
2. Then updated headers only
3. Then modified implementation incrementally
4. Then integrated into main
5. Finally cleaned up legacy code

Each step compiled and was verified before moving forward.

### 2. Preservation of Existing Work
**"Making sure you are not deleting chunks of stuff of how I want it to be"**

Critical rules:
- **Never delete functionality** without explicit permission
- **Always maintain backward compatibility** during migrations
- **Use dual-path approaches** during transitions (e.g., support both old and new APIs simultaneously)
- **Add, don't replace** - prefer adding new code alongside old until migration is complete

**Example:**
During stateless UI refactor, we:
- Kept `GameLogic* logic` field while adding `GameStateView* currentView`
- Supported both rendering paths: `piece = view ? view->board[r][c] : logic->board[r][c]`
- Only removed legacy code after full integration was confirmed working

### 3. No Autonomous Command Execution
**"DONT TRY TO RUN COMMANDS YOURSELF. JUST FINISH AND TELL ME THE COMMAND TO DO"**

Strict command execution policy:
- ✅ **DO**: Provide exact commands for user to copy-paste
- ❌ **DON'T**: Auto-run commands via `run_command` tool
- ✅ **DO**: Wait for user to paste output back
- ❌ **DON'T**: Assume command success without confirmation

**Format for providing commands:**
```bash
make clean
make all
./build/chessgame_gui.exe
```

### 4. Explicit Approval Before Major Changes
**"Tell me when done. I will run make all and test"**

Workflow for major changes:
1. Create implementation plan as artifact
2. Request user review with `notify_user` (BlockedOnUser=true)
3. Wait for explicit "LGTM" or approval
4. Only then proceed with implementation
5. Stop at logical checkpoints for verification

---

## Communication Style

### Planning Phase

#### Task Breakdown
User expects detailed, multi-level task lists:

```markdown
## Phase 1: Infrastructure ✅
- [x] Create event system
- [x] Create state view
- [x] Create controller

## Phase 2: Integration ⏳
- [/] Wire up controller
- [ ] Test rendering
- [ ] Remove legacy code
```

Use:
- `[x]` for completed items
- `[/]` for in-progress items  
- `[ ]` for pending items
- Phases/sections to group related work

#### Implementation Plans
Create detailed plans with:
- **Overview**: What we're doing and why
- **Proposed Changes**: Grouped by component with file links
- **User Review Required**: Highlight breaking changes or important decisions
- **Verification Plan**: How to test the changes

**Example structure:**
```markdown
# Implementation Plan: [Goal]

## Overview
Brief description of problem and solution

## User Review Required
> [!IMPORTANT]  
> Breaking changes that need approval

## Proposed Changes

### Component 1
#### [MODIFY] [file.c](file:///path/to/file.c)
Description of changes

### Component 2
...

## Verification Plan
- Manual test 1: Expected result
- Manual test 2: Expected result
```

### Execution Phase

#### Progress Updates
During implementation:
- Use `task_boundary` to set granular task names
- Update TaskStatus to show current step
- Update TaskSummary to show accumulated progress
- Mark items in task.md as `[/]` when starting, `[x]` when complete

#### Compilation Error Handling
When compilation errors occur:
1. User will paste full error output
2. Fix ALL errors in one batch before asking user to recompile
3. Explain what was fixed and why
4. Provide compile command again

**User's instruction:**
> "First finish all your tasks for main then fix the errors."

This means: Complete logical work unit, then handle all errors together.

### Problem-Solving Approach

#### Debugging Methodology
1. **Understand first**: View relevant code, understand architecture
2. **Diagnose**: Identify root cause, not symptoms
3. **Plan**: Create options (Option A, Option B, etc.)
4. **Present to user**: Let them choose approach
5. **Implement**: Execute chosen solution incrementally
6. **Verify**: Confirm fix works

**Example from session:**
- Bug: Pawn color flashing
- Diagnosis: Race condition between rendering and game logic
- Options: A) Quick mutex fix, B) Full stateless refactor
- User chose: B (architectural solution)
- Implementation: Multi-phase incremental refactor

#### Performance Optimization
User is performance-conscious, expects:
- **Timing instrumentation**: Add performance measurement (like startup timers)
- **Identify bottlenecks**: Show what's taking time
- **Propose optimizations**: Concrete suggestions (e.g., caching move generation)
- **Measure impact**: Before/after comparisons

**User's request:**
> "Also in @[gui/main.c] add timers to see whats taking so long for start up."

This shows they want visibility into performance, not just fixes.

---

## Code Quality Standards

### Code Organization

#### File Structure
User expects:
- Clear separation of concerns (game logic, GUI, AI)
- Proper header/implementation split
- Forward declarations for complex dependencies
- Includes organized logically

#### Naming Conventions
Observed patterns:
- Functions: `module_verb_noun` (e.g., [board_widget_set_state](file:///c:/Users/clash/OneDrive/Desktop/Codes/Java/Hriday%20Chess/ChessGameC/gui/board_widget.c#1768-1794))
- Structs: PascalCase (e.g., `GameStateView`)
- Fields: camelCase (e.g., `selectedRow`)
- Constants: UPPER_SNAKE_CASE

#### Comments
- Use `// NEW:` to mark additions during refactors
- Use `// Legacy:` or `// TODO:` to mark temporary code
- Document WHY, not WHAT
- Mark sections with `// ========== SECTION ==========`

### Code Changes

#### Edit Style
- Use **multi_replace_file_content** for multiple non-contiguous edits
- Use **replace_file_content** for single contiguous block edits
- **Never** try to replace entire files
- Keep edits targeted and minimal

#### Testing Expectations
User does manual testing, expects:
- Clear test scenarios in verification plans
- Expected outcomes stated explicitly
- Step-by-step instructions
- Edge cases considered

**No automated tests exist** - all verification is manual.

---

## Technical Preferences

### Build & Compilation
- Build system: **Make** 
- Compiler: **GCC** (MinGW64)
- Flags: `-std=c11 -Wall -Wextra -O2 -g`
- User compiles manually after being given command
- Build output should be copy-pasted back for error diagnosis

### Architecture Decisions
User favors:
- **Event-driven architecture** over tight coupling
- **Immutable state** (snapshots) over shared mutable state
- **Controller pattern** for coordinating UI and logic
- **Separation of concerns** - UI should not mutate business logic directly

### Performance Philosophy
- **Lazy evaluation** preferred (compute on-demand)
- **Caching** for repeated operations (e.g., move generation)
- **Profiling first** - measure before optimizing
- **Incremental optimization** - one bottleneck at a time

---

## Artifact Management

### Types of Artifacts
User expects these artifact types:

#### 1. task.md (Living Document)
- Checklist of all work items
- Updated as work progresses
- Multi-phase structure
- Marks items as [/] in-progress, [x] completed

#### 2. implementation_plan.md
- Detailed technical plan before execution
- Requires user approval
- Shows file-level changes
- Verification strategy

#### 3. walkthrough.md
- Created AFTER completing work
- Documents what was accomplished
- Shows before/after
- Includes test results

#### 4. Debug/Analysis Docs
- Created when diagnosing complex issues
- Shows investigation process
- Documents findings
- Helps decide on solution

### Artifact Update Frequency
- **task.md**: Update after completing each major step
- **implementation_plan.md**: Create before starting new phase
- **walkthrough.md**: Create at end of major work chunk

---

## Decision-Making Process

### When User Input is Required

**Always ask when:**
- Multiple valid approaches exist
- Breaking changes are involved
- Significant architectural changes
- Performance tradeoffs need balancing
- Deleting existing functionality

**Present as:**
```markdown
## Options

### Option A: [Approach]
**Pros**: ...
**Cons**: ...

### Option B: [Approach]
**Pros**: ...
**Cons**: ...

Which approach would you prefer?
```

### When to Proceed Autonomously

**Can proceed without asking when:**
- Fixing compilation errors (syntax, types, etc.)
- Adding obviously missing includes
- Following approved implementation plan
- Making non-functional improvements (comments, formatting)
- Adding temporary debugging code

---

## Common Patterns from This Session

### Pattern 1: Large Refactoring
**User's approach:**
1. Create detailed plan with all phases
2. Get approval ("LGTM")
3. Execute phase 1 completely
4. Verify compilation
5. Move to phase 2
6. Repeat until complete

### Pattern 2: Debugging
**Process observed:**
1. User reports issue ("pawn color changing")
2. Add debug output to understand behavior
3. Analyze output together
4. Identify root cause
5. Present solution options
6. User chooses approach
7. Implement incrementally
8. Clean up debug output

### Pattern 3: Performance Investigation
**User's request style:**
> "Add timers to see what's taking so long"

This indicates:
- Wants data-driven decisions
- Prefers measurement over guessing
- Expects timing infrastructure added to code
- Will analyze output and decide next steps

### Pattern 4: Integration
**Observed workflow:**
1. Create new components in isolation
2. Test compilation of new components
3. Add them to existing code with compatibility layer
4. Verify old path still works
5. Wire up new path
6. Test new path
7. Remove compatibility layer
8. Clean up

---

## Red Flags - What NOT to Do

### ❌ Don't Auto-Run Commands
User explicitly said not to. Always provide command for them to execute.

### ❌ Don't Make Sweeping Changes
"Making sure you are not deleting chunks of stuff of how I want it to be"
- No massive file replacements
- No deleting without permission
- No "cleaning up" user's intentional code

### ❌ Don't Skip Testing Steps
User wants to verify at each stage. Don't assume success or skip compilation checks.

### ❌ Don't Assume Context
When working on files:
- View the code first
- Understand existing patterns
- Match the user's style
- Ask if unsure

### ❌ Don't Create Unnecessary Abstractions
User values:
- Direct, clear code over clever abstractions
- Explicit over implicit
- Simple over complex

---

## Success Indicators

### You're doing well when:
✅ User says "LGTM" on plans
✅ User says "it works like a charm"
✅ User approves to "continue finishing all the tasks"
✅ No compilation errors after your changes
✅ User provides output showing success

### You need to adjust when:
⚠️ User has to fix compilation errors you introduced
⚠️ User says "fix these errors first"
⚠️ User asks why something was changed
⚠️ User manually reverts your changes

---

## Example Interaction Flow

### Ideal Session Pattern:

1. **User Request**: "Refactor UI Statelessly"

2. **Your Response**:
   - Create task.md breakdown
   - Create implementation_plan.md  
   - Request review: "Please review the plan"

3. **User**: "LGTM"

4. **Your Actions**:
   - Set task_boundary("Phase 1")
   - Implement Phase 1 incrementally
   - Mark items [x] in task.md
   - Notify user: "Phase 1 complete. Please compile:"
   - Provide exact command

5. **User**: [Pastes build output - success]

6. **Your Actions**:
   - Set task_boundary("Phase 2")
   - Continue to next phase
   - Repeat until complete

7. **Final**:
   - Create walkthrough.md
   - Notify user of completion
   - Summarize what was achieved

---

## Chess Game Specific Context

### Project Architecture
```
game/           - Pure C game logic (no GUI dependencies)
  ├── gamelogic.c/h       - Core game state & rules
  ├── gamelogic_movegen.c - Move generation
  ├── gamelogic_safety.c  - Check detection
  ├── piece.c/h           - Piece representation
  ├── move.c/h            - Move representation
  └── ai_engine.cpp       - Stockfish integration

gui/            - GTK4 GUI layer  
  ├── main.c              - Application entry
  ├── board_widget.c/h    - Chess board rendering
  ├── info_panel.c/h      - Side panel UI
  ├── chess_controller.c/h- MVC controller
  ├── game_state_view.c/h - Immutable state snapshot
  └── [various dialogs]   - Settings, themes, etc.
```

### Key Design Decisions
1. **Separation**: Game logic knows nothing about GUI
2. **Event-driven**: UI emits events, controller handles
3. **Immutable views**: UI reads snapshots, never mutates game state
4. **Dual AI support**: Internal (Stockfish) and external engines

### Common Tasks
- Move generation optimization
- UI race condition fixes  
- AI integration improvements
- Theme/customization features
- Tutorial/puzzle systems

---

## Quick Reference Checklist

Before starting work:
- [ ] Understand the full request
- [ ] Check what code exists
- [ ] Create task.md breakdown
- [ ] Create implementation plan if needed
- [ ] Get approval for major changes

During work:
- [ ] Make incremental changes
- [ ] Test compilation frequently
- [ ] Update task.md progress
- [ ] Preserve existing functionality
- [ ] Add comments for clarity

After changes:
- [ ] Verify compilation
- [ ] Provide exact command to user
- [ ] Wait for user confirmation
- [ ] Create walkthrough if significant work
- [ ] Update task status

---

## Final Notes

This user is:
- **Patient and methodical** - prefers correct over fast
- **Detail-oriented** - wants to understand changes
- **Performance-conscious** - cares about efficiency
- **Quality-focused** - values good architecture
- **Collaborative** - wants to guide the process

The best way to work with them:
1. **Plan thoroughly** before implementing
2. **Communicate clearly** at each step
3. **Preserve their work** during changes
4. **Verify frequently** with compilation
5. **Document comprehensively** for their review

Remember: They're building a chess game in C with GTK4. They know what they want architecturally. Your job is to execute their vision incrementally and carefully, not to impose your own design preferences.

When in doubt: **ASK**. They'd rather answer questions than deal with incorrect assumptions.

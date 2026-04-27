---
active: true
iteration: 1
max_iterations: 500
completion_promise: "DONE"
initial_completion_promise: "DONE"
started_at: "2026-04-27T02:54:54.634Z"
session_id: "ses_233238577ffeHN4tHOqW0aKbnM"
ultrawork: true
strategy: "continue"
message_count_at_start: 1
---
1. Using local gcc / linux in docker (orbstack), write https://github.com/id-software/doom and an HTTP 2.0 web server with kqueue (macos auto-detect) / epoll / io_uring (for linux: must select one of the two to be enabled). Then actually run QA using an interactive terminal. Also write reference C11 code that can complexly test the complex C11 spec, and actually compile it.

2. Write a simple C compiler that fully supports the modern C11 spec, in the latest Zig with not a single external dependency, using async, cpu parallelism, multithreads, multiprocess, multicore, and as many features as possible. Aggressively use the /debug skill, debug thoroughly, and make the compiler work properly.

2. (sic — second item numbered 2) In Zig, with not a single external dependency, no performance issues, no memory leaks, the work must continue until the three C language codes written earlier + various C11 edge cases + concern points discovered during work, when run directly with ghidra and gdb debugger (using /debug) under both local gcc / linux in docker (orbstack) environments and via cross-compilation, produce normal and optimized assembly.

ulw teammode tdd commit well

You must form a team without exception, find areas where there could be overlap or where collaboration would show strengths, organize them well, divide RnR, and proceed in team mode.
Plan well, reconstruct and reassign the team per phase so that things proceed in the optimal way. Aggressively utilize ultrabrain, deep, quick, unspecified-low, unspecified-high according to the situation.
Think deeply about team composition in the most efficient way.
If the goal has not been achieved, you cannot move on to the next phase.
Considering each category, give the composed team members clear RnR, and have them agilely propagate and share their statuses with each other insanely frequently and transparently: "Done, To Do, Blockers".

The roadmap must also be managed publicly via gh cli.

All work must be uploaded as GitHub Issues / PRs using gh cli with clear tags, always attaching each agent team member's name in {[NAME]} {Content} format, and if someone has commented something, they must directly notify the other team members. So that reviews can be reflected.
All work history must be status-managed via GitHub issues using gh cli.
All documents must also be managed via GitHub. Be insanely obsessed with documentation. Discoveries, learnings, things to propagate, practices, etc.

Break everything into small pieces, utilize git worktree, create PRs, and only PRs that have passed review may be merged.
Every PR must actually be pulled and run locally to verify:
1. Whether it actually works as described in the PR
2. Whether the code quality is up to standard
3. Whether the test quality is up to standard without cheating (mocking should only be used in the worst cases with justified reasons unavoidably, various edge cases must be well covered in the test code, and **both** finely-divided unit tests AND E2E-equivalent full-flow tests must coexist)
Based on these criteria, approval from both unspecified-high and ultrabrain must always be received before merging.

Create PRs, use gh cli to comment with each other's roles and names attached as [name] {content}, 
On github, communication must be in English without exception, and all team members must also communicate in English. Have them communicate insanely frequently, the main communication channel is github, and when they attach something on github, have them notify the relevant team member via messaging.

Also design the CI in the most ideal structure so that even if mistakes are made, they can be caught through PRs.
Make sure each phase has clear success criteria and verify & QA included. For verify, deep is most optimal; for QA, unspecified-low is most optimal.

Now start work. Asking the user questions or compromising/changing even a single one of the above criteria is forbidden.

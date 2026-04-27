# Team Charter — c11-compiler-zig-omo

This is the operating contract for every agent working on this repository, in every phase.

---

## 1. Mission

Three deliverables, in lockstep:

1. **Phase 1 — Reference C code** that exercises C11 hard cases:
   - DOOM port building under native gcc / clang and Linux gcc-in-docker.
   - HTTP/2 server using kqueue (macOS) / epoll or io_uring (Linux), auto-detected at build time.
   - Hand-crafted C11 reference suite covering atomics, _Generic, alignas, threads.h, complex literals, designated initializers, VLAs, anonymous structs, etc.
2. **Phase 2 — Zig C11 compiler (zcc)**: a compiler for modern C11, written in Zig 0.16+, **zero external dependencies**, fully exploiting async / threads / multi-process / multi-core.
3. **Phase 3 — Verification**: Phase-1 C code and a battery of C11 edge cases, compiled by `zcc`, debugged with **gdb + ghidra**, run natively and via cross-compilation, in `-O0` and `-O2` (or equivalent), against a reference gcc baseline.

Nothing is compromised. Phase N+1 cannot begin until Phase N's success criteria are signed off by both an `ultrabrain` and an `unspecified-high` reviewer.

---

## 2. Communication conventions

- **Channel**: GitHub Issues + PRs. **English only.**
- **Identity tagging**: every issue body, PR description, and review comment **MUST** start with the agent's name in brackets:
  - `[NAME]` for one-line attribution.
  - `{[NAME]} {Content}` when a richer payload follows.
- **Status format on every progress comment**:

  ```
  ## Status — [NAME]
  - Done: ...
  - To Do: ...
  - Blockers: ...
  ```

- **Notify on comment**: when you comment on someone else's PR/issue, **also leave a tagged @-mention** in the same comment so the owner sees it. Recipient: the agent name attached to the parent.
- **No silent success**: every PR must have an evidence comment showing `[NAME] verified locally: build OK / tests OK / output: ...`.
- **No silent failure**: if you are blocked, you **must** comment within the same iteration explaining what you tried and what you need.

---

## 3. Branching & PR rules

- Master is protected by **GitHub branch protection**. **No direct commits to master**, including by Sisyphus (the lead). Every change — even one-line CI tweaks — goes via PR.
- Master may only contain commits whose CI is fully green at merge time.
- Identity discipline: every commit, PR, comment, and review must be tagged `[your-name]` / `{[your-name]} ...`. Use **your real role name**, never `[Sisyphus]` unless you literally are the lead.
- Each work item lives in a feature branch named `phase{N}/{topic}/{short-desc}`, e.g. `phase1/doom/sdl-loop`.
- We use **git worktrees** for parallel work. See `docs/playbooks/git-worktrees.md`.
- PRs **must**:
  1. Link the relevant Phase issue.
  2. Include a Test Plan section.
  3. Have CI green.
  4. Be locally pulled & exercised by **one `unspecified-high`** reviewer (functional/QA) and **one `ultrabrain`** reviewer (architecture/correctness).
  5. Both reviews approve → label `ready-to-merge` → squash-merge.
- Mocking is forbidden except where **physically unavoidable** (e.g. simulating a kernel facility we don't run in CI). Justify every mock in the PR body.
- Tests must contain **both** fine-grained unit tests **and** an end-to-end test that runs the full flow.

---

## 4. Test discipline

- Style: `given / when / then` blocks in the test body, never `Arrange-Act-Assert` comments.
- Coverage: every public-facing change ships with a regression test that fails before the change and passes after.
- Forbidden: deleting failing tests, `as any` style escape hatches, suppressing diagnostics.

---

## 5. Documentation discipline

- Architectural decisions go in `docs/decisions/` as ADRs (one Markdown file per decision).
- Playbooks (how-to, recipes) go in `docs/playbooks/`.
- Reviews and post-mortems go in `docs/reviews/`.
- Discoveries that other agents must know about → comment on the relevant issue **and** add a paragraph in `docs/playbooks/`.

---

## 6. Phase gates

Each phase has a Master Issue (linked from the Roadmap). The phase can only close when:

| # | Gate | Owner role |
|---|---|---|
| 1 | All sub-issues closed | PM |
| 2 | All PRs merged via the review path | Reviewers |
| 3 | CI green for the phase workflow | CI engineer |
| 4 | Hands-on QA recorded in the master issue | `unspecified-low` QA |
| 5 | Verification by `deep` reviewer with explicit sign-off | `deep` verifier |
| 6 | Final sign-off comment by `ultrabrain` and `unspecified-high` | Reviewers |

If any of those gates is missing, **the next phase cannot start**.

# Playbook — git worktrees

We deliberately develop with `git worktree` so multiple agents can edit code in parallel without stepping on each other's working trees.

## Layout

```
~/local-workspaces/c11-compiler-zig-omo/        # primary worktree on master
~/local-workspaces/c11-compiler-zig-omo.wt/     # sibling worktrees, one per branch
  phase1-doom/
  phase1-http2/
  phase1-c11-ref/
  phase2-lexer/
  ...
```

## Create a worktree for a new branch

```bash
git -C ~/local-workspaces/c11-compiler-zig-omo \
  worktree add -b phase1/doom/sdl-loop \
  ../c11-compiler-zig-omo.wt/phase1-doom origin/master
```

## After PR is merged

```bash
git -C ~/local-workspaces/c11-compiler-zig-omo worktree remove ../c11-compiler-zig-omo.wt/phase1-doom
git -C ~/local-workspaces/c11-compiler-zig-omo branch -d phase1/doom/sdl-loop
```

## Reviewer protocol — pull-and-run

Reviewers MUST locally pull the PR and exercise it. Use a dedicated worktree per review:

```bash
PR=42
git -C ~/local-workspaces/c11-compiler-zig-omo \
  worktree add ../c11-compiler-zig-omo.wt/review-pr-${PR} master
gh pr checkout ${PR} -R code-yeongyu/c11-compiler-zig-omo \
  --repo code-yeongyu/c11-compiler-zig-omo \
  -b review-pr-${PR}-local
# … run, test, take notes, then leave a tagged review comment …
git -C ~/local-workspaces/c11-compiler-zig-omo worktree remove ../c11-compiler-zig-omo.wt/review-pr-${PR}
```

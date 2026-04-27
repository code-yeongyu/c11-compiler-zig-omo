#!/usr/bin/env bash
# scripts/worktree.sh — manage parallel git worktrees for the team.
#
# Usage:
#   scripts/worktree.sh new <branch-name>     # creates ../c11-compiler-zig-omo.wt/<sanitized> on a fresh branch
#   scripts/worktree.sh pr <pr-number>        # creates a review worktree from a PR
#   scripts/worktree.sh rm <branch-name>      # removes the worktree for a branch
#   scripts/worktree.sh list                  # lists worktrees

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WT_BASE="$(dirname "$ROOT")/$(basename "$ROOT").wt"
mkdir -p "$WT_BASE"

cmd="${1:?usage: $0 <new|pr|rm|list> ...}"
shift || true

sanitize() { tr -c 'A-Za-z0-9._-' '-' <<<"$1" | sed 's/^-*//; s/-*$//'; }

case "$cmd" in
  new)
    branch="${1:?branch name required}"
    safe="$(sanitize "$branch")"
    target="$WT_BASE/$safe"
    git -C "$ROOT" fetch --quiet origin
    git -C "$ROOT" worktree add -b "$branch" "$target" origin/master
    echo "$target"
    ;;
  pr)
    pr="${1:?pr number required}"
    safe="review-pr-$pr"
    target="$WT_BASE/$safe"
    git -C "$ROOT" worktree add "$target" master
    (cd "$target" && gh pr checkout "$pr" --repo code-yeongyu/c11-compiler-zig-omo -b "review-pr-$pr-local")
    echo "$target"
    ;;
  rm)
    branch="${1:?branch or worktree name required}"
    safe="$(sanitize "$branch")"
    target="$WT_BASE/$safe"
    git -C "$ROOT" worktree remove "$target" --force
    git -C "$ROOT" branch -D "$branch" 2>/dev/null || true
    ;;
  list)
    git -C "$ROOT" worktree list
    ;;
  *)
    echo "unknown command: $cmd" >&2
    exit 2
    ;;
esac

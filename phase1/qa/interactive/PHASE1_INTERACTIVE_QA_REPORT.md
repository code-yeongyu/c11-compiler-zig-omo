# Phase 1 interactive terminal QA report

Operator: `[deep-engineer]`  
Branch: `phase1/qa/interactive-terminal-v1`  
Worktree: `/Users/yeongyu/local-workspaces/c11-compiler-zig-omo.wt/phase1-qa-interactive-terminal-v1`

Each Phase 1 deliverable was run in a live tmux session by an interactive operator [deep-engineer], with transcripts captured. This satisfies the user's 'actually run QA using an interactive terminal' requirement.

## Summary

| Deliverable | tmux session | Interactive command(s) run | Captured artifacts | Observed outcome |
| --- | --- | --- | --- | --- |
| DOOM headless port | `qa-doom`, `qa-doom-run` | `cd phase1/doom && make clean && make HEADLESS=1`; then the Makefile smoke invocation equivalent: `DOOMWADDIR=build DOOM_HEADLESS_LOG=qa/qa-headless.log DOOM_HEADLESS_MAX_FRAMES=64 ./build/linuxxdoom -warp 1 1` | `phase1/qa/interactive/doom/build-transcript.txt`, `phase1/qa/interactive/doom/run-transcript.txt`, `phase1/qa/interactive/doom/run-session-transcript.txt`, `phase1/qa/interactive/doom/qa-headless.log` | Build exited 0. Headless run exited 0. Frame log contains `frame=64` and `DOOM stopped after 64 frames crc32=8904656f`. |
| HTTP/2 server | `qa-http2` with two panes | Pane 1: `cd phase1/http2 && make clean && make all && ./build/h2d --host 127.0.0.1 --port 8000 --ready-file ../qa/interactive/http2/ready.txt`; Pane 2: `curl -v --http2-prior-knowledge http://127.0.0.1:8000/`, `curl -v --http2-prior-knowledge http://127.0.0.1:8000/index.html`, plus 5 sequential h2c curls | `phase1/qa/interactive/http2/server-transcript.txt`, `phase1/qa/interactive/http2/client-transcript.txt` | Server built with the macOS kqueue backend, listened on `127.0.0.1:8000`, served HTTP/2 200 responses for `/` and `/index.html`, handled 5 sequential HTTP/2 requests, then received SIGTERM. Server process exit was 143 due to the explicit SIGTERM. |
| C11 reference suite | `qa-c11ref` | `cd phase1/c11-ref && make clean && make all && make test && make negative` | `phase1/qa/interactive/c11-ref/transcript.txt` | Full sequence exited 0. `make all` compiled all suite sources with gcc and clang, negative gcc/clang checks rejected expected invalid programs, `make test` ran 23 test binaries, and `make negative` passed. |

## DOOM observations

- `make clean && make HEADLESS=1` ran inside tmux and produced a complete compiler transcript.
- The run used the existing Makefile smoke-path assets: `make build/freedoom1.wad` fetched the WAD through the build process, then `build/freedoom1.wad` was copied to `build/doom.wad` exactly as the smoke target does.
- The runtime transcript shows normal DOOM startup through `ST_Init: Init status bar.`
- The frame CRC log is concise and deterministic for this run:

```text
DOOM started (headless)
frame=32 crc32=002166c5
frame=64 crc32=8904656f
headless max frames reached (64)
DOOM stopped after 64 frames crc32=8904656f
```

Quirks: the legacy DOOM code emits many compiler warnings under the repository warning flags. They did not fail the build because `-Werror` is not enabled for the DOOM target.

## HTTP/2 observations

- The server was run in pane 1 and the curl client was run in pane 2 of the same `qa-http2` tmux session.
- `curl -v --http2-prior-knowledge` confirmed h2c operation and showed `HTTP/2 200` responses.
- `/` returned first bytes `/` with `BODY_BYTES=2`.
- `/index.html` returned first bytes `/index.html` with `BODY_BYTES=12`.
- The 5 sequential curl requests to `/` all reported HTTP status 200. Each request was run one after another in the same interactive pane, with per-request tags `[req-1]` through `[req-5]` identifying the individual outcomes.
- The server was terminated after the client run with SIGTERM. The transcript records `Terminated: 15`, and the server marker recorded exit 143. That is the expected shell encoding for SIGTERM, not a functional failure.

Quirk: because `phase1/http2/Makefile` includes `../Makefile.common` before declaring `all`, plain `make` selects the shared `require-linux` target as the default on macOS. The interactive session therefore used the explicit target `make all`, which is the correct project target and builds the macOS kqueue backend.

## C11 reference suite observations

- `make clean`, `make all`, `make test`, and `make negative` were run sequentially in the same live `qa-c11ref` tmux terminal.
- `make all` exited 0 and produced object counts `gcc=46 clang=46`. The count is 46 per compiler because the Makefile compiles both the 23 numbered reference programs and their 23 `_test.c` companions.
- `make test` ran all 23 test executables and exited 0.
- `make negative` re-ran the gcc negative diagnostics and exited 0.

Quirk: `make all` already runs the negative suite for gcc and clang before the explicit `make test` / `make negative` commands. The transcript intentionally keeps the repeated negative output because the requested command sequence was executed exactly.

## Conclusion

The three Phase 1 deliverables were not merely checked through scripted CI. They were built and exercised in persistent tmux terminal sessions, with transcripts captured under `phase1/qa/interactive/`. DOOM produced a 64-frame headless CRC log, the HTTP/2 server handled live HTTP/2 curl traffic from a separate pane, and the C11 reference suite completed its build, positive tests, and negative diagnostic tests interactively.

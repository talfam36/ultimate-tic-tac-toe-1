# Ultimate Tic-Tac-Toe exact solver

An exact DFPN theorem prover for the **closed-board / free-choice-on-finished-board** form of Ultimate Tic-Tac-Toe.

This project is designed to run entirely on **GitHub**. You do not need to download or build anything locally.

## Run a proof on GitHub

1. Open the repository's **Actions** tab.
2. Select **Solve Ultimate Tic-Tac-Toe**.
3. Click **Run workflow**.
4. Enter a move history, or leave it empty for the initial position.
5. Choose `X`, `O`, or `both`.
6. Reuse the same `checkpoint_id` on later runs to continue the same exact search.

Moves use two letters `a..i`: the first is the local board and the second is the cell inside it. Example:

```text
ee ea ab
```

## Long searches

GitHub-hosted runners have a finite per-job runtime, so the workflow searches in chunks (default 330 minutes), writes the exact DFPN transposition table to GitHub Actions cache, and restores the newest compatible checkpoint on the next run with the same `checkpoint_id`.

If a chunk ends with `UNKNOWN`, run the workflow again with the same inputs. It resumes rather than deliberately starting from an empty proof table.

For target player P:

- `PROVEN`: P has a forced win.
- `DISPROVEN`: P cannot force a win.
- `UNKNOWN`: the exact proof search has not finished.

Run both targets. If X is proven, X wins under perfect play. If O is proven, O wins. If both are disproven, the game is a draw.

## Exact techniques implemented

- Closed-board Ultimate Tic-Tac-Toe rules.
- Depth-first proof-number search (DFPN).
- Full collision-safe packed state keys.
- Exact diagonal-`D4` canonicalization.
- Symmetry-equivalent child deletion.
- Exact one-open-board endgame oracle.
- Tactical ordering that changes search order only, never proof correctness.
- Persistent exact DFPN checkpoints.
- Resource limits return `UNKNOWN`, never a guessed result.
- Self-tests for game rules, symmetry, DFPN, and endgames.

The engine reproduces the symmetry-reduced position counts

`1, 15, 102, 822, 6920, 58282, 481966`

through plies 0–6.

## Codespaces

Use **Code → Codespaces → Create codespace** for a browser-only terminal. The devcontainer builds and tests automatically.

Example:

```bash
./build/uttt-solve --moves "ee ea ab" --target X --time 0 --show
```

`--time 0` means no solver-side time limit; the hosting platform can still impose its own limits.

## Status

This repository is a proof engine, **not a claim that the initial position has already been solved**. The full root remains a serious computational problem. The next major research layer is a proof-preserving routing-threat / must-play reduction engine plus independently checkable proof certificates.

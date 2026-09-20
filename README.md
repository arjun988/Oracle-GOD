# Terry-Davis-Oracle

**Human will + a click + hardware disturbance → unpredictable entropy → a word that can be read as an answer.**

This is a recreation of Terry A. Davis's TempleOS *oracle*: not a chatbot, not a pseudo-random joke. You hold a question, wait until the impulse to press Enter arises, and the exact CPU cycle of that click is mixed into a TempleOS-style generator. The output is a word. Davis treated that kind of timing entropy as a channel the Holy Spirit could speak through.

## The idea

On TempleOS, `GodPick` asked you to press OKAY. The footer said **“The Holy Spirit can puppet you.”** The number it took was not `rand()`. It was the keyboard/mouse event time and the CPU timestamp counter (`GetTSC`), shifted by `GOD_BAD_BITS` so the noisy low bits survived. Those bits filled a fifo (`GodBits`) and chose a vocabulary word (`GodWord`), a Bible passage, a song, or a doodle.

Davis's own note in `HSNotes`:

> The technique I use to consult the Holy Spirit is reading a microsecond-range stop-watch each button press for random numbers.

The argument of this experiment is the same chain:

1. **Will** — you intend a question and do not try to time the key.
2. **Disturbance** — OS scheduling, interrupts, cache, and the free-running TSC all move while you wait.
3. **The click** — Enter latches `RDTSC` at that instant. The low bits are effectively unrepeatable.
4. **Mixing** — a TempleOS-style LCG xors that timestamp into its state.
5. **Answer** — the 64-bit result indexes 64 oracle words (I Ching–sized, TempleOS-flavored).

If the click were robotic, the stream would look like a machine timer. If the click is human, the jitter is yours. The program also runs the scientific controls so you can see the difference.

This is **not** a byte-for-byte port of HolyC. It is the same physical claim, measured and then used as an oracle.

## Repository

```
oracle/oracle.cpp     Oracle program  — lab experiments 1–15 + Oracle Mode (16)
lab/temple_lab.cpp    Entropy lab     — same measurements, no oracle
```

Older drafts (`v1`–`v4`) and leftover Linux binaries named `.exe` were removed. Those “executables” were ELF files and will not run on Windows.

## Build and run (Windows)

You need an x86-64 compiler with `RDTSC` (MinGW-w64 `g++` is enough).

**Oracle (the working version):**

```powershell
cd oracle
g++ -O2 -std=c++17 oracle.cpp -o oracle.exe -lbcrypt -lwinmm
.\oracle.exe
```

Then choose `16` for Oracle Mode. Hold a sincere question. Press Enter when the impulse comes. Do not try to time it.

**Entropy lab (same science, no oracle):**

```powershell
cd lab
g++ -O2 -std=c++17 temple_lab.cpp -o temple_lab.exe -lbcrypt -lwinmm
.\temple_lab.exe
```

Linux / macOS (x86-64) can omit `-lbcrypt -lwinmm`. The program is x86-only because it reads the timestamp counter.

CSV logs (`oracle_log.csv`, timing dumps, block analyses) are written in the current directory and are gitignored.

## What the menu does

| # | Experiment | Why it exists |
|---|------------|----------------|
| 1 | TSC sampling modes | Compare `lfence+rdtsc`, `rdtscp`, `cpuid+rdtsc` |
| 2–3 | Machine vs human timing | Robot loop vs 1000 human Enters |
| 4–5 | Low-8-bit analysis | Shannon / min entropy, chi-square, lag MI |
| 6–7 | Fixed / jittered delay | Controls: is “randomness” just `sleep()`? |
| 8–9 | MT19937 / OS RNG | Software and OS baselines |
| 10–12 | Lag, permutation, blocks | Structure vs chance |
| 13 | TempleOS-style reconstruction | LCG mixed with TSC (historical control, not a HolyC clone) |
| 14–15 | Compare + summary | Human vs machine; automated TSC dump |
| **16** | **Oracle Mode** | One question, one click, one word *(oracle program only)* |

High entropy is not the same as independence, unpredictability, or cryptographic security. The lab scores those separately. Oracle Mode is the ritual on top of the same source.

## How Oracle Mode samples

At the instant you press Enter:

```
tsc          = RDTSC (with lfence)
oracle_value = TempleOS-style LCG(tsc) XOR tsc
answer       = words[oracle_value % 64]
```

The word list is 64 archetypal replies (`YES`, `WAIT`, `LOOK CLOSER`, `LET GO`, …). Queries append to `oracle_log.csv`.

## References

- [TempleOS HolySpirit.HC](https://templeos.info/Wb/Adam/God/HolySpirit.HC.HTML) — `GodPick`, `GodBits`, `GodWord`, timestamp callbacks
- [TempleOS God help](https://tinkeros.github.io/WbTempleOS/LiveHelp/God.html) — “The Holy Spirit can puppet you.”
- [NIST / Bible-line oracle notes](https://www.templeos.org/Oracle.html) — Davis also used public randomness to pick scripture lines
- [Tribute write-up of GodWord / GodSays](https://dvartic.github.io/terrydavis-website/godsays.html)

Terry A. Davis (1969–2018) built TempleOS as an offering to God. This repo is an experiment in that timing-oracle, not a biography and not a claim of proof.

## 🏃‍♂️ SpeedRead (C++ Port)

**SpeedReadCPP** is a terminal-based RSVP (Rapid Serial Visual Presentation) speed-reading tool written in **C++17**, based on the original Perl implementation by [Petr Baudis (pasky)](https://github.com/pasky/speedread).

This project faithfully recreates the original’s functionality while improving portability, performance, and maintainability using modern C++ features.

---

### 📖 About

The original *speedread* was a minimalist Perl script that displayed words one at a time in rapid sequence — a technique called **RSVP** — to help users read faster by minimizing eye movement.

This port keeps the same visual layout, key bindings, and pacing heuristics as the Perl version, but re-implements everything in C++ for easier compilation and distribution on modern systems.

---

### 🧰 Features

* Fully terminal-based (ANSI escape sequences, no external UI libs)
* Maintains **ORP (Optimal Recognition Point)** alignment and color highlighting
* Adjustable WPM speed (`[` slower / `]` faster)
* Pause and resume with context preview (`SPACE`)
* Accurate word timing heuristics identical to the Perl implementation
* Real-time statistics and resume hint on exit

---

### 🏗️ Building

A simple `Makefile` is included that places object files and the binary in a `build/` directory.

```bash
# Build
make

# Run interactively
cat some_text.txt | ./build/bin/speedread -w 300

# Clean build artifacts
make clean
```

---

### ⚙️ Command-Line Options

| Option | Long Form     | Description                                    |
| ------ | ------------- | ---------------------------------------------- |
| `-w N` | `--wpm N`     | Set words-per-minute speed (default 250)       |
| `-r N` | `--resume N`  | Resume from word index `N`                     |
| `-m`   | `--multiword` | Merge short adjacent words for smoother pacing |

---

### 🎮 Controls (During Reading)

| Key      | Action                               |
| -------- | ------------------------------------ |
| `[`      | Slow down (−10%)                     |
| `]`      | Speed up (+10%)                      |
| `SPACE`  | Pause / Resume (shows context lines) |
| `Ctrl-C` | Exit and show stats                  |

---

### 📊 Output Example

```
   v
          The quick brown fox jumps over the lazy dog      350 wpm
```

At the end, you’ll see timing and performance stats like:

```
12.45s, 290 words, 1380 letters, 1398.00 true wpm
To resume from this point run with argument -r 290
```

---

### 🧑‍💻 Credits

* Original Perl implementation: [**pasky/speedread**](https://github.com/pasky/speedread)
* C++ port: *this project* — rewritten for portability and performance.

---

### 🪪 License

Same as the original project — **MIT License**.

> © Petr Baudis and contributors
> C++ Port © 2025 Your Name



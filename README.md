# Luplay 🎬🤖

**Luplay** is a ultra-lightweight, hardware-accelerated C++ native video player featuring on-demand offline AI subtitle generation powered by `whisper.cpp`.

## Core Philosophy
- **Bare-Metal Performance**: Zero web wrappers (no Electron/CEF). Built with C++20, Dear ImGui, GLFW, and OpenGL 3.3+.
- **Zero-Wait Playback**: Ultra-efficient playback powered by `libmpv` with forced hardware decoding (`hwdec=auto-safe`).
- **On-Demand AI Heavy Lifting**: Spawns a dedicated background worker thread using `FFmpeg` to demux audio to 16kHz WAV, then passes it to `whisper.cpp` to transcribe directly to standard `.srt` format using GPU compute (CUDA/ROCm/Vulkan/Metal).

---

## Directory Structure

```
Luplay/
├── CMakeLists.txt              # Cross-platform CMake configuration
├── .gitignore                  # Git ignore rules
├── README.md                   # Project documentation & build instructions
├── cmake/
│   ├── FindMPV.cmake           # libmpv library & header module finder
│   └── FindFFmpeg.cmake        # FFmpeg components module finder (avformat, avcodec, swresample, avutil)
├── include/
│   └── luplay/
│       └── app.hpp             # Core Application state interface & declarations
└── src/
    └── main.cpp                # Entry point: GLFW window, OpenGL context, ImGui rendering
```

---

## Dependencies & Prerequisites

### Arch Linux / EndeavourOS
```bash
sudo pacman -S cmake gcc pkgconf glfw-x11 mpv ffmpeg
```

### Windows (vcpkg / MSYS2)
Using `vcpkg`:
```powershell
vcpkg install mpv glfw3 ffmpeg[core,avcodec,avformat,swresample]
```

> **Note on ImGui & whisper.cpp**: `CMakeLists.txt` automatically uses `FetchContent` to download and compile `Dear ImGui` (v1.91.8) with GLFW/OpenGL3 backends and `whisper.cpp` if they are not installed system-wide.

---

## Building & Running

```bash
# 1. Generate build directory
cmake -B build -S .

# 2. Build executable
cmake --build build

# 3. Run Luplay
./build/bin/luplay   # On Windows: .\build\bin\Debug\luplay.exe
```

---

## Application Modes

1. **Playback Mode**: Loads video via `libmpv` using hardware acceleration. Automatically checks for matching `.srt` files in the same directory.
2. **Transcription Mode**: Triggered on demand. Converts audio to 16kHz WAV via `FFmpeg`, feeds buffer to `whisper.cpp`, outputs `.srt`, and auto-reloads subtitles upon completion.

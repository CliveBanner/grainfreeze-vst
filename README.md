# Grainfreeze

**Grainfreeze** is a real-time phase vocoder–based time-stretching and freeze processor. It loads audio into memory and resynthesizes it using FFT analysis and overlap-add techniques, allowing for extreme time-stretching and "tonal" freezing.

![Build Status](https://github.com/${{ github.repository }}/actions/workflows/build.yml/badge.svg)

## Features

*   **Interactive Playhead:** Click and drag the playhead freely. Moving it backwards plays the grains in reverse.
*   **Spectral Freeze Mode:** Loops a tiny slice of audio using crossfading for a continuous "frozen" sound.
*   **Playback Controls:** Adjust playback speed and sound smoothing for various textures.
*   **Tonal Preservation:** Specifically optimized for preserving harmonic content even at zero playback speed.

## Inspiration & Development

**Grainfreeze** was originally developed by [aquanodemusic](https://aquanode.gumroad.com) (original project available on [Gumroad](https://aquanode.gumroad.com/l/Grainfreeze)). It is inspired by the **Audiostretch App** from Bandlab. It was created to provide a VST/Standalone equivalent for tonal zero-playback-speed freezing, which is often missing in standard time-stretching tools.

The project was developed with the assistance of AI (Claude) and is completely **open source**. If you are a C++ or DSP expert, feel free to dive into the code and improve the FFT implementation further!

## Technical Notes

*   **Algorithm:** Resynthesis via FFT analysis and overlap-add techniques.
*   **Focus:** The primary goal is the preservation of **tonality**. 
*   **Limitation:** Due to the nature of the phase vocoder algorithm, transients will be heavily smeared.

## Building from Source

This project is built using [JUCE](https://juce.com/) and CMake.

### Local Build
1. Clone the repository:
   ```bash
   git clone https://github.com/YOUR_USERNAME/grainfreeze-vst.git
   cd grainfreeze-vst
   ```
2. Configure and build:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

### CI/CD (Multi-platform Binaries)
Binaries for **Windows, macOS, and Linux** are automatically generated for every push to the `main` branch. You can find them in the **Actions** tab or the **Releases** section of the GitHub repository.

---
*Original Author: [aquanodemusic](https://aquanode.gumroad.com) | Inspired by Audiostretch*

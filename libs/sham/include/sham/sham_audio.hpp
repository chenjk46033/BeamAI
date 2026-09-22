#pragma once

#include <cstdint>
#include <vector>

namespace beam::sham {

// Port of BeamV0/GUIMatlab/BEAM/Sham/whiteNoise.m:
//   wn = 2*(rand(1, round(duration*Fs)) - 0.5);
// Uniform noise in [-1, 1). `seed` makes it reproducible (MATLAB's `rand`
// draws from the global stream; there's no source seed to match, so this
// is a deliberate, disclosed choice for testability).
std::vector<double> whiteNoise(double durationSeconds, double fs, std::uint32_t seed = 0);

// Port of the signal-assembly core of
// BeamV0/GUIMatlab/BEAM/Sham/setShamAudio.m. The MATLAB source's
// `load('sonicationSound.mat')` and `sound()` / `audiowrite()` I/O are not
// ported -- `soundData`/`fs` are passed in, and the assembled buffer is
// returned instead of played/written. The unused `randFlag` argument is
// dropped.
//
// Builds a `round(duration*fs)`-sample buffer: an optional low-level white
// noise bed (0.005 * whiteNoise when backgroundNoise is true), then a copy
// of the burst sound (`soundData`, tiled/truncated to `bd` seconds) added
// in every `bi` seconds, each offset by an extra 0.3 s.
std::vector<double> setShamAudio(const std::vector<double>& soundData, double fs, double durationSeconds,
                                  double bd, double bi, bool backgroundNoise, std::uint32_t noiseSeed = 0);

}  // namespace beam::sham

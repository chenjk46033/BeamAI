#include "sham/sham_audio.hpp"

#include <cmath>
#include <random>

namespace beam::sham {

std::vector<double> whiteNoise(double durationSeconds, double fs, std::uint32_t seed) {
    const long n = std::lround(durationSeconds * fs);
    std::vector<double> out(n < 0 ? 0 : static_cast<std::size_t>(n));

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);  // rand() -> [0,1)
    for (double& v : out) {
        v = 2.0 * (dist(rng) - 0.5);
    }
    return out;
}

std::vector<double> setShamAudio(const std::vector<double>& soundData, double fs, double durationSeconds,
                                  double bd, double bi, bool backgroundNoise, std::uint32_t noiseSeed) {
    const long shamLen = std::lround(durationSeconds * fs);
    const std::size_t len = shamLen < 0 ? 0 : static_cast<std::size_t>(shamLen);

    std::vector<double> shamData;
    if (backgroundNoise) {
        shamData = whiteNoise(durationSeconds, fs, noiseSeed);
        for (double& v : shamData) v *= 0.005;
    } else {
        shamData.assign(len, 0.0);
    }

    const long nBursts = static_cast<long>(std::floor(durationSeconds / bi));
    const long bDs = static_cast<long>(std::floor(bd * fs));
    const long bIs = static_cast<long>(std::floor(bi * fs));
    const long soundLen = static_cast<long>(soundData.size());

    // clipSound: the burst sound tiled/truncated to bDs samples.
    std::vector<double> clipSound;
    if (bDs <= soundLen) {
        clipSound.assign(soundData.begin(), soundData.begin() + (bDs < 0 ? 0 : bDs));
    } else if (soundLen > 0) {
        const double nRepeats = static_cast<double>(bDs) / static_cast<double>(soundLen);
        const long whole = static_cast<long>(std::floor(nRepeats));
        clipSound.reserve(static_cast<std::size_t>(bDs));
        for (long r = 0; r < whole; ++r) {
            clipSound.insert(clipSound.end(), soundData.begin(), soundData.end());
        }
        const double remainder = std::fmod(nRepeats, 1.0);
        const long remainderS = std::lround(remainder * static_cast<double>(soundLen));
        clipSound.insert(clipSound.end(), soundData.begin(), soundData.begin() + remainderS);
    }

    const long clipSoundS = static_cast<long>(clipSound.size());
    const long offset = std::lround(0.3 * fs);
    constexpr double amplitude = 1.0;

    for (long i = 0; i < nBursts; ++i) {
        const long burstStart0 = i * bIs + offset;  // MATLAB 1-based burstStart minus 1
        // MATLAB guards: burstStart < 1  ->  burstStart0 < 0
        //                burstStart > length(shamData) - clipSoundS
        //                  -> burstStart0 + 1 > shamLen - clipSoundS
        if (burstStart0 < 0) continue;
        if (burstStart0 + 1 > shamLen - clipSoundS) continue;
        for (long s = 0; s < clipSoundS; ++s) {
            shamData[static_cast<std::size_t>(burstStart0 + s)] += amplitude * clipSound[static_cast<std::size_t>(s)];
        }
    }
    return shamData;
}

}  // namespace beam::sham

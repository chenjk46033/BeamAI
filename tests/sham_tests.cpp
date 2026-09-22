#include <vector>

#include <gtest/gtest.h>

#include "sham/sham_audio.hpp"

using namespace beam::sham;

TEST(WhiteNoise, LengthRangeAndReproducibility) {
    const std::vector<double> a = whiteNoise(1.0, 10.0, /*seed=*/42);
    ASSERT_EQ(a.size(), 10u);
    for (double v : a) {
        EXPECT_GE(v, -1.0);
        EXPECT_LT(v, 1.0);
    }
    EXPECT_EQ(a, whiteNoise(1.0, 10.0, 42));            // same seed -> identical
    EXPECT_NE(a, whiteNoise(1.0, 10.0, 7));             // different seed -> different
}

TEST(WhiteNoise, ZeroDurationIsEmpty) {
    EXPECT_TRUE(whiteNoise(0.0, 44100.0).empty());
}

TEST(SetShamAudio, SingleBurstPlacedAtOffset) {
    // soundData 4 samples, fs 10, duration 1s (10 samples), bd 0.4s (bDs=4,
    // clipSound = whole soundData), bi 0.5s (bIs=5). nBursts=2, offset=3.
    // burst 0 -> start 3, fits. burst 1 -> start 8, guard rejects (8+1 > 10-4).
    const std::vector<double> sound = {1, 1, 1, 1};
    const std::vector<double> sham = setShamAudio(sound, 10.0, 1.0, 0.4, 0.5, /*backgroundNoise=*/false);

    const std::vector<double> expected = {0, 0, 0, 1, 1, 1, 1, 0, 0, 0};
    EXPECT_EQ(sham, expected);
}

TEST(SetShamAudio, TilesSoundWhenBurstLongerThanClip) {
    // soundData [2,3], bd 0.6s @ fs 10 -> bDs=6 > 2 -> tiled to [2,3,2,3,2,3].
    // duration 2s -> 20 samples. bi 1s -> bIs=10. nBursts=2, offset=3.
    const std::vector<double> sound = {2, 3};
    const std::vector<double> sham = setShamAudio(sound, 10.0, 2.0, 0.6, 1.0, false);

    ASSERT_EQ(sham.size(), 20u);
    EXPECT_DOUBLE_EQ(sham[3], 2.0);
    EXPECT_DOUBLE_EQ(sham[8], 3.0);
    EXPECT_DOUBLE_EQ(sham[13], 2.0);
    EXPECT_DOUBLE_EQ(sham[18], 3.0);
    EXPECT_DOUBLE_EQ(sham[0], 0.0);
    EXPECT_DOUBLE_EQ(sham[9], 0.0);
    EXPECT_DOUBLE_EQ(sham[19], 0.0);
}

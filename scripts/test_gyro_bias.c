// Run from the repository root:
// clang -std=c11 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I UltraXReal/UltraXReal/Vendor/fusion scripts/test_gyro_bias.c UltraXReal/UltraXReal/Vendor/fusion/FusionOffset.c -lm -o /tmp/test_gyro_bias && /tmp/test_gyro_bias
#include "FusionOffset.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

static FusionVector vector(float x, float y, float z) {
    return (FusionVector) {.axis = {.x = x, .y = y, .z = z}};
}
static const FusionVector gravity = {.axis = {.x = 0, .y = 0, .z = 1}};

static void warmupDrift(unsigned int rate) {
    FusionOffset offset;
    FusionOffsetInitialise(&offset, 1000); // Deliberately different from actual delivery rate.
    FusionOffsetSetBias(&offset, 0, 0, 0.5f);
    float totalYaw = 0, lateYaw = 0;
    for (unsigned int i = 0; i < 60 * rate; i++) {
        const float noise = 0.01f * sinf(2 * 3.14159265f * 7 * i / rate);
        const FusionVector output = FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 0.62f + noise), gravity, 1.0f / rate);
        totalYaw += output.axis.z / rate;
        if (i >= 10 * rate) { lateYaw += output.axis.z / rate; }
    }
    assert(fabsf(totalYaw) < 0.35f); // Old fixed bias drifts about 7.2 degrees.
    assert(fabsf(lateYaw) < 0.03f);
    assert(fabsf(FusionOffsetGetBias(&offset).axis.z - 0.62f) < 0.002f);
    printf("%u Hz: 60-second drift %.4f degrees; after settling %.4f\n", rate, totalYaw, lateYaw);
}

static void preservesTurns(void) {
    FusionOffset offset;
    FusionOffsetInitialise(&offset, 1000);
    FusionOffsetSetBias(&offset, 0, 0, 0.5f);
    float yaw = 0;
    for (int i = 0; i < 20000; i++) {
        FusionVector output = FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 1), gravity, 0.001f);
        yaw += output.axis.z * 0.001f;
    }
    assert(fabsf(yaw - 10) < 0.01f); // Preserve a deliberately slow 0.5 dps pan.
    assert(FusionOffsetGetBias(&offset).axis.z == 0.5f);
    FusionVector output = FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 30.5f), gravity, 0.001f);
    assert(output.axis.z == 30); // Immediate response to a fast turn.
}

static void rejectsMotionAndGaps(void) {
    FusionOffset offset;
    FusionOffsetInitialise(&offset, 1000);
    FusionOffsetSetBias(&offset, 0, 0, 0);
    for (int i = 0; i < 10000; i++) {
        const float acceleration = 0.04f * sinf(2 * 3.14159265f * i / 1000);
        FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 0.1f), vector(acceleration, 0, 1), 0.001f);
    }
    assert(FusionOffsetGetBias(&offset).axis.z == 0);
    FusionOffsetSetBias(&offset, 0, 0, 0);
    for (int i = 0; i < 10000; i++) {
        FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, i % 2 ? 0.2f : -0.1f), gravity, 0.001f);
    }
    assert(FusionOffsetGetBias(&offset).axis.z == 0); // Excessive variance is not stillness.
    FusionOffsetSetBias(&offset, 0, 0, 0);
    for (int i = 0; i < 1500; i++) {
        FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 0.1f), gravity, 0.001f);
    }
    FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 0.1f), gravity, 2.0f);
    for (int i = 0; i < 1500; i++) {
        FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 0.1f), gravity, 0.001f);
    }
    assert(FusionOffsetGetBias(&offset).axis.z == 0); // Do not join stillness across a data gap.
}

// Deterministic noise resembling the live glasses trace: gyro sigma ~0.075 dps,
// acceleration sigma ~0.01g. Instantaneous thresholds used to reject every window.
static uint32_t noiseState = 123456789;
static float sensorNoise(void) {
    float result = -6;
    for (int i = 0; i < 12; i++) {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        result += (float)(noiseState >> 8) / 16777216.0f;
    }
    return result;
}

static void noisyStationarySensor(void) {
    FusionOffset offset;
    FusionOffsetInitialise(&offset, 1000);
    FusionOffsetSetBias(&offset, 0, 0, 0);
    float lateYaw = 0;
    for (int i = 0; i < 30000; i++) {
        FusionVector gyro = vector(0.01f + 0.055f * sensorNoise(),
                                  -0.016f + 0.065f * sensorNoise(),
                                  0.022f + 0.075f * sensorNoise());
        FusionVector acceleration = vector(0.21f + 0.003f * sensorNoise(),
                                           -0.007f + 0.005f * sensorNoise(),
                                           0.969f + 0.01f * sensorNoise());
        FusionVector output = FusionOffsetUpdateWithAcceleration(&offset, gyro, acceleration, 0.001f);
        if (i >= 10000) { lateYaw += output.axis.z * 0.001f; }
    }
    assert(fabsf(FusionOffsetGetBias(&offset).axis.z - 0.022f) < 0.005f);
    assert(fabsf(lateYaw) < 0.03f);
    printf("Noisy stationary sensor: residual yaw after settling %.4f degrees\n", lateYaw);
}

static void explicitCalibration(void) {
    FusionOffset offset;
    FusionOffsetInitialise(&offset, 1000);
    FusionOffsetBeginCalibration(&offset);
    for (int i = 0; i < 2000; i++) {
        FusionOffsetUpdateWithAcceleration(&offset, vector(0.2f, -0.3f, 0.7f), gravity, 0.001f);
    }
    assert(FusionOffsetGetCalibrationState(&offset) == FusionOffsetCalibrationComplete);
    assert(fabsf(FusionOffsetGetBias(&offset).axis.z - 0.7f) < 0.001f);
    FusionOffsetBeginCalibration(&offset);
    FusionOffsetUpdateWithAcceleration(&offset, vector(0, 0, 5), gravity, 0.001f);
    assert(FusionOffsetGetCalibrationState(&offset) == FusionOffsetCalibrationMoved);
}

int main(void) {
    warmupDrift(60);
    warmupDrift(250);
    warmupDrift(1000);
    preservesTurns();
    rejectsMotionAndGaps();
    explicitCalibration();
    noisyStationarySensor();
    puts("Gyro bias regression tests passed.");
}

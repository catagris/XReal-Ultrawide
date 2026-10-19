/**
 * @file FusionOffset.c
 * @author Seb Madgwick
 * @brief Gyroscope offset correction algorithm for run-time calibration of the
 * gyroscope offset.
 */

//------------------------------------------------------------------------------
// Includes

#include "FusionOffset.h"
#include <math.h> // fabsf

//------------------------------------------------------------------------------
// Definitions

/**
 * @brief Cutoff frequency in Hz.
 */
#define CUTOFF_FREQUENCY (0.02f)

/**
 * @brief Timeout in seconds.
 */
#define TIMEOUT (5)

/**
 * @brief Threshold in degrees per second.
 */
#define THRESHOLD (3.0f)

// Require a full quiet window before refining the stored calibration. The tighter
// calibrated threshold preserves slow head turns that the original 3 dps gate
// would otherwise learn as bias. Sub-threshold constant yaw remains unobservable
// without an external heading reference.
#define REFINEMENT_THRESHOLD (0.25f)
#define STATIONARY_PERIOD (2.0f)
#define ACCELERATION_CHANGE (0.015f)
#define GYROSCOPE_VARIANCE (0.01f)
#define MOTION_FILTER_TIME_CONSTANT (0.05f)

static void ResetStationaryWindow(FusionOffset *const offset) {
    offset->stationaryElapsed = 0;
    offset->stationarySamples = 0;
    offset->stationaryMean = FUSION_VECTOR_ZERO;
    offset->stationaryM2 = FUSION_VECTOR_ZERO;
    offset->stationaryAcceleration = FUSION_VECTOR_ZERO;
}

//------------------------------------------------------------------------------
// Functions

/**
 * @brief Initialises the gyroscope offset algorithm.
 * @param offset Gyroscope offset algorithm structure.
 * @param sampleRate Sample rate in Hz.
 */
void FusionOffsetInitialise(FusionOffset *const offset, const unsigned int sampleRate) {
    offset->sampleRate = sampleRate > 0 ? sampleRate : 1000;
    offset->filterCoefficient = 2.0f * (float) M_PI * CUTOFF_FREQUENCY * (1.0f / (float) offset->sampleRate);
    offset->timeout = TIMEOUT * offset->sampleRate;
    offset->timer = 0;
    offset->gyroscopeOffset = FUSION_VECTOR_ZERO;
    offset->calibrationState = FusionOffsetCalibrationIdle;
    offset->calibrationSamples = 0;
    offset->calibrationMean = FUSION_VECTOR_ZERO;
    offset->calibrationM2 = FUSION_VECTOR_ZERO;
    offset->manualBias = false;
    offset->motionFilterInitialised = false;
    ResetStationaryWindow(offset);
}

void FusionOffsetBeginCalibration(FusionOffset *const offset) {
    ResetStationaryWindow(offset);
    offset->calibrationSamples = 0;
    offset->calibrationMean = FUSION_VECTOR_ZERO;
    offset->calibrationM2 = FUSION_VECTOR_ZERO;
    offset->calibrationState = FusionOffsetCalibrationCollecting;
}

void FusionOffsetCancelCalibration(FusionOffset *const offset) {
    offset->calibrationState = FusionOffsetCalibrationIdle;
}

FusionOffsetCalibrationState FusionOffsetGetCalibrationState(const FusionOffset *const offset) {
    return offset->calibrationState;
}

FusionVector FusionOffsetGetBias(const FusionOffset *const offset) {
    return offset->gyroscopeOffset;
}

void FusionOffsetSetBias(FusionOffset *const offset, float x, float y, float z) {
    offset->gyroscopeOffset = (FusionVector) {.axis = {.x = x, .y = y, .z = z}};
    // Use a tighter stationary threshold once an initial calibration is known.
    offset->manualBias = true;
    offset->motionFilterInitialised = false;
    offset->timer = 0;
    ResetStationaryWindow(offset);
}

/**
 * @brief Updates the gyroscope offset algorithm and returns the corrected
 * gyroscope measurement.
 * @param offset Gyroscope offset algorithm structure.
 * @param gyroscope Gyroscope measurement in degrees per second.
 * @return Corrected gyroscope measurement in degrees per second.
 */
FusionVector FusionOffsetUpdate(FusionOffset *const offset, FusionVector gyroscope) {
    // Compatibility for callers without acceleration or device timestamps.
    const FusionVector gravity = {.axis = {.x = 0, .y = 0, .z = 1}};
    return FusionOffsetUpdateWithAcceleration(offset, gyroscope, gravity, 1.0f / offset->sampleRate);
}

FusionVector FusionOffsetUpdateWithAcceleration(FusionOffset *const offset, FusionVector gyroscope,
                                               FusionVector accelerometer, float deltaTime) {

    if (offset->calibrationState == FusionOffsetCalibrationCollecting) {
        offset->calibrationSamples++;
        for (unsigned int axis = 0; axis < 3; axis++) {
            const float value = gyroscope.array[axis];
            if (!isfinite(value) || fabsf(value) > THRESHOLD) {
                offset->calibrationState = FusionOffsetCalibrationMoved;
                break;
            }
            const float delta = value - offset->calibrationMean.array[axis];
            offset->calibrationMean.array[axis] += delta / offset->calibrationSamples;
            offset->calibrationM2.array[axis] += delta * (value - offset->calibrationMean.array[axis]);
        }
        if (offset->calibrationState == FusionOffsetCalibrationCollecting &&
            offset->calibrationSamples >= 2 * offset->sampleRate) {
            bool still = true;
            for (unsigned int axis = 0; axis < 3; axis++) {
                // Reject variance above (0.08 degrees/second)^2.
                still = still && offset->calibrationM2.array[axis] /
                    (offset->calibrationSamples - 1) < 0.0064f;
            }
            if (still) {
                FusionOffsetSetBias(offset, offset->calibrationMean.axis.x,
                                    offset->calibrationMean.axis.y, offset->calibrationMean.axis.z);
                offset->calibrationState = FusionOffsetCalibrationComplete;
            } else {
                offset->calibrationState = FusionOffsetCalibrationMoved;
            }
        }
        return FusionVectorSubtract(gyroscope, offset->gyroscopeOffset);
    }

    const FusionVector corrected = FusionVectorSubtract(gyroscope, offset->gyroscopeOffset);
    const float threshold = offset->manualBias ? REFINEMENT_THRESHOLD : THRESHOLD;
    if (!isfinite(deltaTime) || deltaTime <= 0 || deltaTime > 0.1f) {
        offset->motionFilterInitialised = false;
        ResetStationaryWindow(offset);
        return corrected;
    }
    for (unsigned int axis = 0; axis < 3; axis++) {
        if (!isfinite(corrected.array[axis]) || !isfinite(accelerometer.array[axis]) ||
            fabsf(corrected.array[axis]) > THRESHOLD) {
            offset->motionFilterInitialised = false;
            ResetStationaryWindow(offset);
            return corrected;
        }
    }

    // Detect motion from smoothed measurements, not individual noisy 1 kHz
    // samples. This filter never touches the gyro signal sent to the AHRS.
    if (!offset->motionFilterInitialised) {
        offset->motionGyroscope = corrected;
        offset->motionAcceleration = accelerometer;
        offset->motionFilterInitialised = true;
    } else {
        const float alpha = 1.0f - expf(-deltaTime / MOTION_FILTER_TIME_CONSTANT);
        offset->motionGyroscope = FusionVectorAdd(offset->motionGyroscope,
            FusionVectorMultiplyScalar(FusionVectorSubtract(corrected, offset->motionGyroscope), alpha));
        offset->motionAcceleration = FusionVectorAdd(offset->motionAcceleration,
            FusionVectorMultiplyScalar(FusionVectorSubtract(accelerometer, offset->motionAcceleration), alpha));
    }
    const float accelerationSquared = FusionVectorMagnitudeSquared(offset->motionAcceleration);
    if (accelerationSquared < 0.9f * 0.9f || accelerationSquared > 1.1f * 1.1f) {
        ResetStationaryWindow(offset);
        return corrected;
    }
    for (unsigned int axis = 0; axis < 3; axis++) {
        if (fabsf(offset->motionGyroscope.array[axis]) > threshold) {
            ResetStationaryWindow(offset);
            return corrected;
        }
    }
    if (offset->stationarySamples == 0) {
        offset->stationaryAcceleration = offset->motionAcceleration;
    } else if (FusionVectorMagnitudeSquared(FusionVectorSubtract(offset->motionAcceleration, offset->stationaryAcceleration)) >
               ACCELERATION_CHANGE * ACCELERATION_CHANGE) {
        ResetStationaryWindow(offset);
        return corrected;
    }

    offset->stationarySamples++;
    offset->stationaryElapsed += deltaTime;
    for (unsigned int axis = 0; axis < 3; axis++) {
        const float delta = gyroscope.array[axis] - offset->stationaryMean.array[axis];
        offset->stationaryMean.array[axis] += delta / offset->stationarySamples;
        offset->stationaryM2.array[axis] += delta * (gyroscope.array[axis] - offset->stationaryMean.array[axis]);
    }
    if (offset->stationaryElapsed < STATIONARY_PERIOD || offset->stationarySamples < 2) {
        return corrected;
    }
    bool still = true;
    for (unsigned int axis = 0; axis < 3; axis++) {
        still = still && offset->stationaryM2.array[axis] / (offset->stationarySamples - 1) < GYROSCOPE_VARIANCE;
    }
    if (still) {
        const FusionVector mean = offset->stationaryMean;
        // Refine velocity bias; never reset the orientation or screen anchor.
        FusionOffsetSetBias(offset, mean.axis.x, mean.axis.y, mean.axis.z);
    } else {
        ResetStationaryWindow(offset);
    }
    return FusionVectorSubtract(gyroscope, offset->gyroscopeOffset);
}

//------------------------------------------------------------------------------
// End of file

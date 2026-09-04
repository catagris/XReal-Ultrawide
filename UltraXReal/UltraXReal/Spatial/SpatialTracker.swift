import Foundation
import Combine
import simd

/// Viewport state derived from head tracking.
struct SpatialState {
    var viewportOffset: CGPoint = .zero  // pixel offset on the virtual canvas
    var zoomLevel: CGFloat = 1.0         // <1 = screen smaller/farther, >1 = magnified
    var rollAngle: CGFloat = 0           // head roll, radians — view counter-rotates
    var isCalibrated: Bool = false
}

/// Apparent screen size: scales how much canvas the glasses show at once.
/// Smaller = the screen looks farther away (more visible, less panning).
enum SpatialScreenSize: String, CaseIterable, Codable {
    case huge, large, medium, small

    var scale: CGFloat {
        switch self {
        case .huge: return 1.0    // 1:1 pixels, closest/biggest
        case .large: return 0.8
        case .medium: return 0.65
        case .small: return 0.5   // whole 4K canvas visible at once
        }
    }

    var label: String {
        switch self {
        case .huge: return "Huge (1:1, near)"
        case .large: return "Large"
        case .medium: return "Medium"
        case .small: return "Small (fit all)"
        }
    }
}

/// Sensitivity presets for head tracking.
enum SpatialSensitivity: String, CaseIterable, Codable {
    case low, medium, high

    /// Pixels per radian of yaw rotation.
    /// The Air 2 Pro shows 1920px across ~40° (~0.7 rad), so ~2700 px/rad makes
    /// the canvas hold still in space as the head turns (world-locked).
    /// Below that the screen partially follows the head; above it overshoots.
    var pixelsPerRadian: CGFloat {
        switch self {
        case .low: return 2000
        case .medium: return 2700
        case .high: return 3400
        }
    }
}

/// Smoothing presets for head tracking.
enum SpatialSmoothing: String, CaseIterable, Codable {
    case responsive, balanced, smooth

    /// EMA alpha (higher = more responsive, lower = smoother).
    var alpha: CGFloat {
        switch self {
        case .responsive: return 0.3
        case .balanced: return 0.15
        case .smooth: return 0.05
        }
    }
}

/// Converts IMU quaternions into viewport offsets and zoom levels.
///
/// Subscribes to XRealIMUService orientation updates, extracts yaw/pitch,
/// applies dead zone + EMA smoothing, and maps to viewport coordinates.
final class SpatialTracker: ObservableObject {

    @Published private(set) var state = SpatialState()

    var sensitivity: SpatialSensitivity = .medium
    var smoothing: SpatialSmoothing = .balanced
    var screenScale: CGFloat = SpatialScreenSize.medium.scale
    var leanToZoomEnabled: Bool = true
    var zoomSensitivity: CGFloat = 2.0
    var zoomThreshold: CGFloat = 0.15  // radians of pitch before zoom kicks in

    /// Canvas and viewport dimensions (set by SpatialRenderer).
    var canvasSize: CGSize = CGSize(width: 3840, height: 2160)
    var viewportSize: CGSize = CGSize(width: 1920, height: 1080)

    private let deadZone: CGFloat = 0.005  // ~0.3 degrees in radians
    private var smoothedYaw: CGFloat = 0
    private var smoothedPitch: CGFloat = 0
    private var smoothedRoll: CGFloat = 0
    private var neutralPitch: CGFloat = 0

    private weak var imuService: XRealIMUService?
    private var cancellable: AnyCancellable?
    private var connectionCancellable: AnyCancellable?

    /// Recenter on the first sample after every (re)connect, so the neutral
    /// pose comes from real data rather than a timer that may fire before
    /// the IMU is streaming.
    private var needsInitialRecenter = true

    init(imuService: XRealIMUService) {
        self.imuService = imuService

        cancellable = imuService.orientationSubject
            .receive(on: DispatchQueue.main)
            .sink { [weak self] rawQuat in
                guard let self else { return }
                if self.needsInitialRecenter {
                    self.needsInitialRecenter = false
                    self.recenter()
                    return
                }
                let relQuat = imuService.relativeOrientation
                self.update(orientation: relQuat)
            }

        connectionCancellable = imuService.$isConnected
            .receive(on: DispatchQueue.main)
            .sink { [weak self] connected in
                if !connected {
                    self?.needsInitialRecenter = true
                }
            }
    }

    /// Reset tracker to center and recenter the IMU reference.
    func recenter() {
        imuService?.recenter()
        smoothedYaw = 0
        smoothedPitch = 0
        smoothedRoll = 0
        neutralPitch = 0
        state = SpatialState(viewportOffset: .zero, zoomLevel: screenScale, rollAngle: 0, isCalibrated: true)
    }

    // MARK: - Private

    private func update(orientation: simd_quatf) {
        // Extract yaw, pitch, roll from quaternion (ZYX Euler convention)
        let (yaw, pitch, roll) = eulerAngles(from: orientation)

        // Dead zone: ignore micro-movements
        let filteredYaw = abs(yaw - smoothedYaw) > deadZone ? yaw : smoothedYaw
        let filteredPitch = abs(pitch - smoothedPitch) > deadZone ? pitch : smoothedPitch
        let filteredRoll = abs(roll - smoothedRoll) > deadZone ? roll : smoothedRoll

        // Exponential moving average
        let alpha = smoothing.alpha
        smoothedYaw += (filteredYaw - smoothedYaw) * alpha
        smoothedPitch += (filteredPitch - smoothedPitch) * alpha
        smoothedRoll += (filteredRoll - smoothedRoll) * alpha

        // Screen size sets the base zoom (<1 samples a larger canvas region,
        // so the screen appears smaller/farther); lean-to-zoom magnifies on top.
        var zoom = screenScale
        if leanToZoomEnabled {
            let pitchDelta = smoothedPitch - neutralPitch
            if pitchDelta > zoomThreshold {
                zoom *= 1.0 + (pitchDelta - zoomThreshold) * zoomSensitivity
            }
        }
        zoom = max(0.5, min(zoom, 2.5))

        let sampleW = viewportSize.width / zoom
        let sampleH = viewportSize.height / zoom

        // World-locked mapping: the canvas stays fixed in space, the head
        // steers the viewport across it — head right → viewport moves right,
        // head up → viewport moves up.
        let maxOffsetX = max(0, canvasSize.width - sampleW)
        let maxOffsetY = max(0, canvasSize.height - sampleH)
        let centerX = maxOffsetX / 2
        let centerY = maxOffsetY / 2

        var offsetX = centerX + (smoothedYaw * sensitivity.pixelsPerRadian)
        var offsetY = centerY - (smoothedPitch * sensitivity.pixelsPerRadian)

        offsetX = max(0, min(offsetX, maxOffsetX))
        offsetY = max(0, min(offsetY, maxOffsetY))

        state = SpatialState(
            viewportOffset: CGPoint(x: offsetX, y: offsetY),
            zoomLevel: zoom,
            rollAngle: smoothedRoll,
            isCalibrated: true
        )
    }

    /// Extract yaw, pitch and roll from a quaternion (ZYX Euler convention).
    private func eulerAngles(from q: simd_quatf) -> (yaw: CGFloat, pitch: CGFloat, roll: CGFloat) {
        let ix = q.imag.x
        let iy = q.imag.y
        let iz = q.imag.z
        let r = q.real

        // Yaw (Z-axis rotation)
        let siny = 2.0 * (r * iz + ix * iy)
        let cosy = 1.0 - 2.0 * (iy * iy + iz * iz)
        let yaw = CGFloat(atan2(siny, cosy))

        // Pitch (Y-axis rotation)
        let sinp = 2.0 * (r * iy - iz * ix)
        let pitch: CGFloat
        if abs(sinp) >= 1 {
            pitch = CGFloat(copysign(Float.pi / 2, sinp))
        } else {
            pitch = CGFloat(asin(sinp))
        }

        // Roll (X-axis rotation)
        let sinr = 2.0 * (r * ix + iy * iz)
        let cosr = 1.0 - 2.0 * (ix * ix + iy * iy)
        let roll = CGFloat(atan2(sinr, cosr))

        return (yaw, pitch, roll)
    }
}

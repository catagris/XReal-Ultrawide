#include <metal_stdlib>
using namespace metal;

struct ViewportUniforms {
    float2 viewportOrigin;  // Top-left of crop rect, normalized [0,1]
    float2 viewportSize;    // Size of crop rect, normalized [0,1]
    float2 canvasSizePx;    // canvas pixel dimensions (for aspect-correct roll)
    float rollAngle;        // radians; view counter-rotates to stay level
    float _pad;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

// Fullscreen quad — triangle strip, 4 vertices, no vertex buffer needed
vertex VertexOut spatialVertex(uint vertexID [[vertex_id]]) {
    float2 positions[4] = {
        float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1)
    };
    float2 texCoords[4] = {
        float2(0, 1), float2(1, 1), float2(0, 0), float2(1, 0)
    };

    VertexOut out;
    out.position = float4(positions[vertexID], 0, 1);
    out.texCoord = texCoords[vertexID];
    return out;
}

// Samples a viewport sub-region of the canvas texture
fragment float4 spatialFragment(
    VertexOut in [[stage_in]],
    texture2d<float> canvasTexture [[texture(0)]],
    constant ViewportUniforms &uniforms [[buffer(0)]]
) {
    // Map output texcoord [0,1] to the viewport region on the canvas,
    // rotating around the viewport center by the head roll angle so the
    // screen stays level in space when the head tilts. Rotation is done in
    // pixel space to stay aspect-correct.
    float2 centerN = uniforms.viewportOrigin + 0.5 * uniforms.viewportSize;
    float2 dPx = (in.texCoord - 0.5) * uniforms.viewportSize * uniforms.canvasSizePx;
    float cs = cos(uniforms.rollAngle);
    float sn = sin(uniforms.rollAngle);
    float2 rotated = float2(cs * dPx.x - sn * dPx.y, sn * dPx.x + cs * dPx.y);
    float2 sampleCoord = centerN + rotated / uniforms.canvasSizePx;

    constexpr sampler s(filter::linear, address::clamp_to_edge);
    if (any(sampleCoord < 0.0) || any(sampleCoord > 1.0)) {
        return float4(0, 0, 0, 1);  // outside the canvas: black, not edge-smear
    }
    return canvasTexture.sample(s, sampleCoord);
}

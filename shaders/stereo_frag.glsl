#version 330 core
// SplatDiff: combine the two eye images (see include/Stereo.hpp).

uniform sampler2D LeftEye;
uniform sampler2D RightEye;
uniform int Mode;         // 1 colour anaglyph, 2 grey anaglyph, 3 side-by-side, 4 side-by-side + lens
uniform int SwapEyes;
uniform vec2 Resolution;  // window size in pixels
uniform vec2 Distortion;  // k1, k2

out vec4 FragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / Resolution;

    if (Mode == 1 || Mode == 2) {
        vec3 L = texture(LeftEye, uv).rgb;
        vec3 R = texture(RightEye, uv).rgb;
        float lumL = dot(L, vec3(0.299, 0.587, 0.114));
        // red filter over the left eye sees only R; cyan filter over the right eye sees G + B
        FragColor = vec4(Mode == 1 ? L.r : lumL, R.g, R.b, 1.0);
        return;
    }

    // side by side: which half, and the coordinate inside that half
    bool rightHalf = uv.x >= 0.5;
    vec2 e = vec2(rightHalf ? uv.x * 2.0 - 1.0 : uv.x * 2.0, uv.y);

    if (Mode == 4) {
        // Barrel pre-distortion around the centre of each half. For each output pixel, sample the eye image
        // further out: r_src = r (1 + k1 r^2 + k2 r^4), scaled so the middle of the side edges still fits.
        float aspect = (Resolution.x * 0.5) / Resolution.y;
        vec2 c = (e - 0.5) * 2.0;
        c.x *= aspect;
        float r2 = dot(c, c);
        float scale = 1.0 + Distortion.x * r2 + Distortion.y * r2 * r2;
        float fit = 1.0 + Distortion.x * aspect * aspect + Distortion.y * aspect * aspect * aspect * aspect;
        c = c * scale / fit;
        c.x /= aspect;
        e = c * 0.5 + 0.5;
        if (any(lessThan(e, vec2(0.0))) || any(greaterThan(e, vec2(1.0)))) {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    bool useRight = rightHalf != (SwapEyes == 1);
    FragColor = vec4(useRight ? texture(RightEye, e).rgb : texture(LeftEye, e).rgb, 1.0);
}

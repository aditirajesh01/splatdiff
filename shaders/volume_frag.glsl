#version 330 core
// SplatDiff: ray-march the change field D = O_B - O_A through the detector's voxel grid.
// One ray per pixel, front-to-back compositing with early termination.

uniform sampler3D Occupancy;  // r = O_A, g = O_B, trilinear filtering
uniform mat4 InvViewProj;     // clip -> world
uniform mat4 InvModel;        // world -> model (grid) space
uniform vec3 BoxMin;          // grid bounds in model space
uniform vec3 BoxMax;
uniform vec2 Resolution;
uniform float StepSize;       // ray step (model units), about half a voxel
uniform float Voxel;          // voxel size, for opacity correction
uniform float Opacity;        // opacity per voxel when |D| = 1
uniform float Threshold;      // |D| below this is transparent
uniform int ShowContext;      // draw occupied, unchanged space as faint grey
uniform float ContextOpacity;

out vec4 FragColor;

void main()
{
    // ray through this pixel, from the near to the far plane, in model space
    vec2 ndc = gl_FragCoord.xy / Resolution * 2.0 - 1.0;
    vec4 nearW = InvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 farW = InvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ro = (InvModel * vec4(nearW.xyz / nearW.w, 1.0)).xyz;
    vec3 rf = (InvModel * vec4(farW.xyz / farW.w, 1.0)).xyz;
    vec3 rd = normalize(rf - ro);

    // slab test against the grid's box
    vec3 inv = 1.0 / rd;
    vec3 t0 = (BoxMin - ro) * inv, t1 = (BoxMax - ro) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tNear = max(max(tmin.x, tmin.y), max(tmin.z, 0.0));
    float tFar = min(min(tmax.x, tmax.y), tmax.z);
    if (tFar <= tNear) discard;

    vec3 extent = BoxMax - BoxMin;
    vec3 colour = vec3(0.0);
    float alpha = 0.0;
    // per-pixel jitter of the start point hides wood-grain banding
    float t = tNear + StepSize * fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float exponent = StepSize / Voxel;

    for (int i = 0; i < 4096 && t < tFar; ++i, t += StepSize) {
        vec2 o = texture(Occupancy, (ro + rd * t - BoxMin) / extent).rg;
        float d = o.g - o.r;
        float m = abs(d);
        vec3 c;
        float a;
        if (m > Threshold) {
            c = d > 0.0 ? vec3(0.2, 1.0, 0.3) : vec3(1.0, 0.15, 0.15);
            a = clamp(Opacity * (m - Threshold) / (1.0 - Threshold), 0.0, 0.99);
        } else if (ShowContext == 1 && max(o.r, o.g) > 0.5) {
            c = vec3(0.75);
            a = ContextOpacity;
        } else {
            continue;
        }
        a = 1.0 - pow(1.0 - a, exponent);  // opacity correction for the step length
        colour += (1.0 - alpha) * a * c;
        alpha += (1.0 - alpha) * a;
        if (alpha > 0.98) break;  // early ray termination
    }
    if (alpha < 0.002) discard;
    FragColor = vec4(colour, alpha);  // premultiplied
}

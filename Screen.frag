#version 430 core
in vec2 uv;
out vec4 FragColor;
layout(binding = 0) uniform sampler2D uNative;
layout(binding = 1) uniform sampler2D uPreviewImage;
layout(binding = 3) uniform sampler2D uBloom;
uniform float uExposure;
uniform int uGravity, uIndicator;
uniform vec2 uScreenSize;
vec3 filmic(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec4 high = texture(uNative, uv);
    vec3 hdr = high.a > 0.5 ? high.rgb : texture(uPreviewImage, uv).rgb;
    vec3 bloom = texture(uBloom, uv).rgb;
    vec3 mapped = filmic((hdr + 0.10 * bloom) * uExposure);
    vec3 srgb = mix(12.92 * mapped, 1.055 * pow(mapped, vec3(1.0 / 2.4)) - 0.055,
                    step(vec3(0.0031308), mapped));
    if (uIndicator != 0) {
        vec2 pixel = vec2(gl_FragCoord.x, uScreenSize.y - gl_FragCoord.y);
        float badge = 1.0 - smoothstep(6.0, 7.0, length(pixel - vec2(22.0)));
        srgb = mix(srgb, uGravity != 0 ? vec3(0.20, 0.85, 0.47) : vec3(0.80, 0.22, 0.20), badge);
    }
    FragColor = vec4(srgb, 1.0);
}

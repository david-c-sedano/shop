#version 100

precision mediump float;

varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec2 resolution;

void main() {
    // curves 
    vec2 p = fragTexCoord * 2.0 - 1.0;
    p *= 1.0 + 0.035 * dot(p, p);
    vec2 uv = p * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // scanlines
    vec3 color = texture2D(texture0, uv).rgb;
    float scan = sin(uv.y * resolution.y * 3.14159);
    color *= 0.92 + 0.08 * scan;

    // vignette
    vec2 v = uv * (1.0 - uv.yx);
    float vig = pow(clamp(v.x * v.y * 18.0, 0.0, 1.0), 0.18);
    color *= vig;

    gl_FragColor = vec4(color, 1.0);
}

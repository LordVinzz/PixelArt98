// Internal implementation fragment for GpuEffectRenderer shader sources.
// This file is included by its owning translation unit; do not compile it separately.

constexpr const char* kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFragmentShaderPreamble = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_source;
uniform sampler2D u_depth;
uniform sampler2D u_mask;
uniform int u_mode;
uniform vec4 u_size;
uniform vec4 u_params;
uniform vec4 u_params2;
uniform int u_curve_point_count;
uniform vec4 u_curve_x0;
uniform vec4 u_curve_x1;
uniform vec4 u_curve_y0;
uniform vec4 u_curve_y1;
uniform vec4 u_primary;
uniform vec4 u_secondary;

float luminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 rgb_to_hsv(vec3 c) {
    vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, k.wz), vec4(c.gb, k.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 0.00001;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv_to_rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 1.0 / 3.0, 2.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amp * value_noise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return value;
}

vec4 sample_source(vec2 uv) {
    return texture(u_source, clamp(uv, vec2(0.0), vec2(1.0)));
}

vec4 fetch_source_or_transparent(ivec2 p) {
    if (p.x < 0 || p.y < 0 || p.x >= int(u_size.x) || p.y >= int(u_size.y)) {
        return vec4(0.0);
    }
    return texelFetch(u_source, p, 0);
}

vec4 sample_source_nearest_transparent(vec2 p) {
    return fetch_source_or_transparent(ivec2(floor(p + vec2(0.5))));
}

vec4 sample_source_bilinear_transparent(vec2 p) {
    ivec2 base = ivec2(floor(p));
    vec2 t = fract(p);
    vec4 p00 = fetch_source_or_transparent(base);
    vec4 p10 = fetch_source_or_transparent(base + ivec2(1, 0));
    vec4 p01 = fetch_source_or_transparent(base + ivec2(0, 1));
    vec4 p11 = fetch_source_or_transparent(base + ivec2(1, 1));
    return mix(mix(p00, p10, t.x), mix(p01, p11, t.x), t.y);
}

vec4 affine_transform(vec2 uv) {
    vec2 dest = floor(uv * u_size.xy);
    float scale = max(0.001, u_params.y);
    vec2 center = (u_size.xy - vec2(1.0)) * 0.5;
    vec2 d = (dest - u_params.zw - center) / scale;
    float c = cos(u_params.x);
    float s = sin(u_params.x);
    vec2 source = vec2(c * d.x + s * d.y, -s * d.x + c * d.y) + center;
    if (u_params2.x > 0.5) {
        return sample_source_bilinear_transparent(source);
    }
    return sample_source_nearest_transparent(source);
}

float curve_x_at(int index) {
    return index < 4 ? u_curve_x0[index] : u_curve_x1[index - 4];
}

float curve_y_at(int index) {
    return index < 4 ? u_curve_y0[index] : u_curve_y1[index - 4];
}

float curve_slope(int segment) {
    float width = max(0.001, curve_x_at(segment + 1) - curve_x_at(segment));
    return (curve_y_at(segment + 1) - curve_y_at(segment)) / width;
}

float curve_tangent(int point, int count) {
    if (point == 0) return curve_slope(0);
    if (point == count - 1) return curve_slope(count - 2);
    float left_slope = curve_slope(point - 1);
    float right_slope = curve_slope(point);
    if (left_slope * right_slope <= 0.0) return 0.0;
    float left_width = curve_x_at(point) - curve_x_at(point - 1);
    float right_width = curve_x_at(point + 1) - curve_x_at(point);
    float first_weight = 2.0 * right_width + left_width;
    float second_weight = right_width + 2.0 * left_width;
    return (first_weight + second_weight) /
           (first_weight / left_slope + second_weight / right_slope);
}

float evaluate_point_curve(float value) {
    float target = clamp(value, 0.0, 1.0);
    int count = clamp(u_curve_point_count, 2, 8);
    if (target <= curve_x_at(0)) {
        return clamp(curve_y_at(0), 0.0, 1.0);
    }
    for (int i = 1; i < 8; ++i) {
        if (i < count) {
            float x0 = clamp(curve_x_at(i - 1), 0.0, 1.0);
            float x1 = clamp(curve_x_at(i), x0 + 0.001, 1.0);
            if (target <= x1 || i == count - 1) {
                float y0 = clamp(curve_y_at(i - 1), 0.0, 1.0);
                float y1 = clamp(curve_y_at(i), 0.0, 1.0);
                float width = max(0.001, x1 - x0);
                float t = clamp((target - x0) / width, 0.0, 1.0);
                float t2 = t * t;
                float t3 = t2 * t;
                float left_tangent = curve_tangent(i - 1, count);
                float right_tangent = curve_tangent(i, count);
                return clamp((2.0 * t3 - 3.0 * t2 + 1.0) * y0 +
                             (t3 - 2.0 * t2 + t) * width * left_tangent +
                             (-2.0 * t3 + 3.0 * t2) * y1 +
                             (t3 - t2) * width * right_tangent, 0.0, 1.0);
            }
        }
    }
    return clamp(curve_y_at(count - 1), 0.0, 1.0);
}

vec4 blur_box(vec2 uv, int radius) {
    vec4 sum = vec4(0.0);
    float count = 0.0;
    int r = clamp(radius, 1, 16);
    for (int y = -16; y <= 16; ++y) {
        for (int x = -16; x <= 16; ++x) {
            if (abs(x) <= r && abs(y) <= r) {
                sum += sample_source(uv + vec2(float(x) * u_size.z, float(y) * u_size.w));
                count += 1.0;
            }
        }
    }
    return sum / max(1.0, count);
}

int depth_of_field_radius(vec2 uv) {
    float depth = texture(u_depth, clamp(uv, vec2(0.0), vec2(1.0))).r * 255.0;
    float focus = clamp(u_params.x, 0.0, 1.0) * 255.0;
    float aperture = clamp(u_params.y, 0.0, 1.0);
    float falloff = max(1.0, u_params.z);
    float max_radius = clamp(u_params.w, 1.0, 32.0);
    float amount = clamp(abs(depth - focus) / falloff, 0.0, 1.0) * aperture;
    return int(floor(amount * max_radius + 0.5001));
}

vec4 motion_blur(vec2 uv, int radius, float angle) {
    vec2 dir = vec2(cos(angle), sin(angle));
    vec4 sum = vec4(0.0);
    float count = 0.0;
    int r = clamp(radius, 1, 24);
    for (int i = -24; i <= 24; ++i) {
        if (abs(i) <= r) {
            vec2 offset = dir * vec2(u_size.z, u_size.w) * float(i);
            sum += sample_source(uv + offset);
            count += 1.0;
        }
    }
    return sum / max(1.0, count);
}

vec4 radial_zoom_blur(vec2 uv, int amount, bool radial) {
    vec2 center = clamp(u_params2.xy, vec2(0.0), vec2(1.0));
    vec2 delta = uv - center;
    vec4 sum = vec4(0.0);
    float count = 0.0;
    float strength = clamp(float(amount) / 100.0, 0.0, 1.0);
    for (int i = 0; i < 16; ++i) {
        float t = (float(i) / 15.0 - 0.5) * strength;
        vec2 sample_uv = radial ? center + mat2(cos(t), -sin(t), sin(t), cos(t)) * delta
                                : uv - delta * t;
        sum += sample_source(sample_uv);
        count += 1.0;
    }
    return sum / max(1.0, count);
}

vec4 median_approx(vec2 uv, int radius) {
    vec4 c = sample_source(uv);
    vec4 b = blur_box(uv, radius);
    return mix(c, b, 0.45);
}

vec4 edge_color(vec2 uv) {
    float left = luminance(sample_source(uv + vec2(-u_size.z, 0.0)).rgb);
    float right = luminance(sample_source(uv + vec2(u_size.z, 0.0)).rgb);
    float up = luminance(sample_source(uv + vec2(0.0, -u_size.w)).rgb);
    float down = luminance(sample_source(uv + vec2(0.0, u_size.w)).rgb);
    float edge = clamp(abs(right - left) + abs(down - up), 0.0, 1.0);
    return vec4(vec3(edge), sample_source(uv).a);
}

)GLSL";

constexpr const char* kFragmentShaderEffects = R"GLSL(
vec4 apply_effect(vec2 uv, vec4 src) {
    vec2 pixel = uv * u_size.xy;
    int mode = u_mode;

    if (mode == 0) {
        float brightness = u_params.x;
        float contrast = 1.0 + u_params.y * 2.0;
        vec3 color = (src.rgb - 0.5) * contrast + 0.5 + brightness;
        return vec4(clamp(color, 0.0, 1.0), src.a);
    }
    if (mode == 1) {
        vec3 hsv = rgb_to_hsv(src.rgb);
        hsv.x = fract(hsv.x + u_params.x);
        hsv.y = clamp(hsv.y + u_params.y, 0.0, 1.0);
        hsv.z = clamp(hsv.z + u_params.z, 0.0, 1.0);
        return vec4(hsv_to_rgb(hsv), src.a);
    }
    if (mode == 2) {
        float in_black = clamp(u_params.x, 0.0, 0.99);
        float in_white = max(in_black + 0.01, u_params.y);
        float gamma = max(0.05, u_params.z);
        float out_black = clamp(u_params.w, 0.0, 1.0);
        float out_white = clamp(u_params2.x, 0.0, 1.0);
        vec3 normalized = clamp((src.rgb - in_black) / (in_white - in_black), 0.0, 1.0);
        normalized = pow(normalized, vec3(1.0 / gamma));
        return vec4(mix(vec3(out_black), vec3(out_white), normalized), src.a);
    }
    if (mode == 3 || mode == 4) {
        float steps = max(2.0, u_params.x);
        vec3 color = floor(src.rgb * (steps - 1.0) + 0.5) / (steps - 1.0);
        if (mode == 4) {
            float n = hash12(pixel) - 0.5;
            color = floor(clamp(src.rgb + n / steps, 0.0, 1.0) * (steps - 1.0) + 0.5) / (steps - 1.0);
        }
        return vec4(color, src.a);
    }
    if (mode == 5) {
        vec3 color = src.rgb;
        float lo = min(min(color.r, color.g), color.b);
        float hi = max(max(color.r, color.g), color.b);
        color = clamp((color - lo) / max(0.01, hi - lo), 0.0, 1.0);
        return vec4(color, src.a);
    }
    if (mode == 6) return vec4(vec3(luminance(src.rgb)), src.a);
    if (mode == 7) {
        return vec4(vec3(dot(src.rgb, vec3(0.393, 0.769, 0.189)),
                         dot(src.rgb, vec3(0.349, 0.686, 0.168)),
                         dot(src.rgb, vec3(0.272, 0.534, 0.131))), src.a);
    }
    if (mode == 8) return vec4(1.0 - src.rgb, src.a);
    if (mode == 9) return vec4(src.rgb, 1.0 - src.a);
    if (mode == 10) {
        float levels = max(2.0, u_params.x);
        return vec4(floor(src.rgb * (levels - 1.0) + 0.5) / (levels - 1.0), src.a);
    }
    if (mode == 11) return mix(src, blur_box(uv, int(u_params.x)), 0.65);
    if (mode == 12) {
        vec4 edge = edge_color(uv);
        vec4 soft = blur_box(uv, max(1, int(u_params.x / 2.0)));
        return vec4(mix(edge.rgb, soft.rgb, clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    if (mode == 13) {
        vec4 gray = vec4(vec3(luminance(src.rgb)), src.a);
        vec4 edge = edge_color(uv);
        return vec4(clamp(gray.rgb - edge.rgb * clamp(u_params.y / 100.0, 0.0, 1.0), 0.0, 1.0), src.a);
    }
    if (mode == 14) return blur_box(uv, int(u_params.x));
    if (mode == 15) return motion_blur(uv, int(u_params.x), u_params.z);
    if (mode == 16) return radial_zoom_blur(uv, int(u_params.y), true);
    if (mode == 17) return radial_zoom_blur(uv, int(u_params.y), false);
    if (mode == 18) return median_approx(uv, int(u_params.x));
    if (mode == 19) {
        vec4 b = blur_box(uv, int(u_params.x));
        float diff = length(src.rgb - b.rgb);
        return diff < u_params.y / 255.0 ? b : src;
    }
    if (mode == 20 || mode == 21) {
        float cell = max(1.0, u_params.x);
        vec2 snapped = (floor(pixel / cell) * cell + cell * 0.5) / u_size.xy;
        return sample_source(snapped);
    }
    if (mode == 22) {
        float radius = max(1.0, u_params.x);
        vec2 jitter = vec2(hash12(pixel), hash12(pixel + 19.7)) * 2.0 - 1.0;
        return sample_source(uv + jitter * vec2(u_size.z, u_size.w) * radius);
    }
    if (mode == 23) {
        vec2 center = clamp(u_params2.xy, vec2(0.0), vec2(1.0));
        vec2 delta = uv - center;
        float dist = length(delta);
        vec2 sample_uv = center + delta * (1.0 - u_params.x * 0.35 * (1.0 - dist));
        return sample_source(sample_uv);
    }
    if (mode == 24) {
        vec2 center = clamp(u_params2.xy, vec2(0.0), vec2(1.0));
        vec2 delta = uv - center;
        float radius = length(delta) / max(0.05, u_params.y * 0.75);
        float angle = u_params.x * 6.28318 * (1.0 - clamp(radius, 0.0, 1.0));
        vec2 sample_uv = center + mat2(cos(angle), -sin(angle), sin(angle), cos(angle)) * delta;
        return sample_source(sample_uv);
    }
    if (mode == 25) {
        float cell = max(1.0, u_params.x);
        vec2 local = mod(pixel, cell) / cell;
        vec2 tile = floor(pixel / cell) * cell;
        vec2 mirrored = mix(local, 1.0 - local, step(0.5, local));
        return sample_source((tile + mirrored * cell) / u_size.xy);
    }
    if (mode == 26) {
        float n = fbm(pixel / max(1.0, u_params.x));
        vec2 offset = (vec2(n, value_noise(pixel.yx / max(1.0, u_params.x))) - 0.5) * u_params.y * vec2(u_size.z, u_size.w);
        return sample_source(uv + offset);
    }
    if (mode == 27) {
        vec2 center = vec2(0.5);
        vec2 delta = uv - center;
        float radius = length(delta);
        float angle = atan(delta.y, delta.x);
        vec2 sample_uv = center + vec2(cos(angle), sin(angle)) * (1.0 - radius) * u_params.x;
        return sample_source(sample_uv);
    }
    if (mode == 28) {
        float n = hash12(pixel + u_params.x);
        float coverage = clamp(u_params.y / 100.0, 0.0, 1.0);
        if (n > coverage) return src;
        vec3 noise = mix(vec3(n), vec3(hash12(pixel + 2.0), hash12(pixel + 7.0), hash12(pixel + 13.0)), clamp(u_params.z / 100.0, 0.0, 1.0));
        return vec4(mix(src.rgb, noise, clamp(u_params.x / 255.0, 0.0, 1.0)), src.a);
    }
    if (mode == 29) return median_approx(uv, int(u_params.x));
    if (mode == 30) {
        vec4 b = blur_box(uv, int(u_params.x));
        vec3 color = src.rgb + b.rgb * clamp(u_params.y / 100.0, 0.0, 1.0);
        return vec4(clamp(color, 0.0, 1.0), src.a);
    }
    if (mode == 31) {
        vec3 hsv = rgb_to_hsv(src.rgb);
        if (hsv.x < 0.06 || hsv.x > 0.94) {
            hsv.y *= 1.0 - clamp(u_params.y / 100.0, 0.0, 1.0);
        }
        return vec4(hsv_to_rgb(hsv), src.a);
    }
    if (mode == 32) {
        vec4 n = src * 5.0 - sample_source(uv + vec2(u_size.z, 0.0)) - sample_source(uv - vec2(u_size.z, 0.0))
                 - sample_source(uv + vec2(0.0, u_size.w)) - sample_source(uv - vec2(0.0, u_size.w));
        return vec4(mix(src.rgb, clamp(n.rgb, 0.0, 1.0), clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    if (mode == 33) {
        vec4 b = blur_box(uv, int(u_params.x));
        return vec4(mix(src.rgb, b.rgb * vec3(1.05, 1.0, 0.94), clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    if (mode == 34) {
        float d = distance(uv, clamp(u_params2.xy, vec2(0.0), vec2(1.0))) / max(0.1, u_params.x);
        float v = smoothstep(0.9, 0.2, d + u_params.y / 400.0);
        return vec4(src.rgb * v, src.a);
    }
    if (mode == 35) {
        float n = fbm(pixel / max(1.0, u_params.x));
        return vec4(mix(u_primary.rgb, u_secondary.rgb, n), src.a);
    }
    if (mode == 36 || mode == 37) {
        vec2 z = (uv - 0.5) * max(0.1, 3.0 / max(0.1, u_params.x));
        vec2 c = mode == 36 ? vec2(-0.8, 0.156) : z;
        if (mode == 36) z = vec2(cos(u_params.z), sin(u_params.z)) * 0.2 + z;
        float iter = 0.0;
        for (int i = 0; i < 64; ++i) {
            z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
            if (dot(z, z) > 4.0) break;
            iter += 1.0;
        }
        vec3 color = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + iter * 0.18);
        return vec4(color, src.a);
    }
    if (mode == 38) {
        float n = fbm(pixel / max(1.0, u_params.x));
        return vec4(mix(src.rgb, vec3(n), 0.65), src.a);
    }
    if (mode == 39) return edge_color(uv);
    if (mode == 40 || mode == 42) {
        float l0 = luminance(sample_source(uv - vec2(u_size.z, u_size.w)).rgb);
        float l1 = luminance(sample_source(uv + vec2(u_size.z, u_size.w)).rgb);
        float e = clamp((l0 - l1) + 0.5, 0.0, 1.0);
        return vec4(vec3(e), src.a);
    }
    if (mode == 41) {
        vec4 e = edge_color(uv);
        return vec4(mix(src.rgb, vec3(0.0), e.r * clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    if (mode == 43) return affine_transform(uv);
    if (mode == 44) {
        float white_point = clamp(u_params.x, -1.0, 1.0);
        float highlights = clamp(u_params.y, -1.0, 1.0);
        float shadows = clamp(u_params.z, -1.0, 1.0);
        float black_point = clamp(u_params.w, -1.0, 1.0);
        float black_anchor = clamp(black_point * 0.20, -0.20, 0.20);
        float white_anchor = clamp(1.0 - white_point * 0.20, 0.80, 1.20);
        vec3 color = clamp((src.rgb - vec3(black_anchor)) / max(0.05, white_anchor - black_anchor), 0.0, 1.0);
        float lum = clamp(luminance(src.rgb), 0.0, 1.0);
        float shadow_weight = pow(1.0 - lum, 1.6);
        float highlight_weight = pow(lum, 1.6);
        color += shadows * 0.45 * shadow_weight * (shadows >= 0.0 ? (vec3(1.0) - color) : color);
        color += highlights * 0.45 * highlight_weight * (highlights >= 0.0 ? (vec3(1.0) - color) : color);
        return vec4(clamp(color, 0.0, 1.0), src.a);
    }
    if (mode == 45) {
        vec3 color = src.rgb;
        if (u_params2.x > 0.5) {
            float lum = clamp(luminance(color), 0.0, 1.0);
            float mapped_lum = evaluate_point_curve(lum);
            if (lum > 0.0001) {
                color *= mapped_lum / lum;
            } else {
                color = vec3(mapped_lum);
            }
        } else {
            if (u_params2.y > 0.5) color.r = evaluate_point_curve(color.r);
            if (u_params2.z > 0.5) color.g = evaluate_point_curve(color.g);
            if (u_params2.w > 0.5) color.b = evaluate_point_curve(color.b);
        }
        return vec4(clamp(color, 0.0, 1.0), src.a);
    }
    if (mode == 46) {
        float temperature = clamp(u_params.x, -1.0, 1.0);
        float warm = max(temperature, 0.0);
        float cool = max(-temperature, 0.0);
        vec3 offset = vec3(warm * 45.0 - cool * 28.0,
                           warm * 12.0 + cool * 8.0,
                           -warm * 35.0 + cool * 45.0) / 255.0;
        return vec4(clamp(src.rgb + offset, 0.0, 1.0), src.a);
    }
    if (mode == 47) {
        int radius = depth_of_field_radius(uv);
        if (radius <= 0) return src;
        return blur_box(uv, radius);
    }
    return src;
}

void main() {
    vec4 src = sample_source(v_uv);
    float mask = texture(u_mask, v_uv).r;
    if (mask < 0.5) {
        frag_color = src;
        return;
    }
    frag_color = clamp(apply_effect(v_uv, src), 0.0, 1.0);
}
)GLSL";

const std::string& combined_fragment_shader_source() {
    static const std::string source =
        std::string(kFragmentShaderPreamble) + kFragmentShaderEffects;
    return source;
}

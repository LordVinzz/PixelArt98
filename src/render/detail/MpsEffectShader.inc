// Internal implementation fragment for MpsEffectRenderer shader source.
// This file is included by its owning translation unit; do not compile it separately.

constexpr const char* kMetalSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    int mode;
    uint width;
    uint height;
    uint curve_point_count;
    float4 params;
    float4 params2;
    float4 curve_x0;
    float4 curve_x1;
    float4 curve_y0;
    float4 curve_y1;
    float4 primary;
    float4 secondary;
};

float luma(float3 color) {
    return dot(color, float3(0.299, 0.587, 0.114));
}

float3 rgb_to_hsv(float3 c) {
    float4 k = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = mix(float4(c.bg, k.wz), float4(c.gb, k.xy), step(c.b, c.g));
    float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 0.00001;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv_to_rgb(float3 c) {
    float3 p = abs(fract(c.xxx + float3(0.0, 1.0 / 3.0, 2.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(float3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

float hash12(float2 p) {
    float3 p3 = fract(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + float2(1.0, 0.0));
    float c = hash12(i + float2(0.0, 1.0));
    float d = hash12(i + float2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(float2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amp * value_noise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return value;
}

float4 read_px(texture2d<float, access::read> source, int2 p, constant Uniforms& u) {
    int2 clamped = clamp(p, int2(0), int2(int(u.width) - 1, int(u.height) - 1));
    return source.read(uint2(clamped));
}

float4 blur_box(texture2d<float, access::read> source, int2 p, constant Uniforms& u, int radius) {
    float4 sum = float4(0.0);
    float count = 0.0;
    int r = clamp(radius, 1, 16);
    for (int y = -16; y <= 16; ++y) {
        for (int x = -16; x <= 16; ++x) {
            if (abs(x) <= r && abs(y) <= r) {
                sum += read_px(source, p + int2(x, y), u);
                count += 1.0;
            }
        }
    }
    return sum / max(1.0, count);
}

float4 motion_blur(texture2d<float, access::read> source, int2 p, constant Uniforms& u) {
    int radius = clamp(int(u.params.x), 1, 24);
    float2 direction = float2(cos(u.params.z), sin(u.params.z));
    float4 sum = float4(0.0);
    float count = 0.0;
    for (int i = -24; i <= 24; ++i) {
        if (abs(i) <= radius) {
            sum += read_px(source, p + int2(round(direction * float(i))), u);
            count += 1.0;
        }
    }
    return sum / max(1.0, count);
}

float4 radial_zoom_blur(texture2d<float, access::read> source, int2 p, constant Uniforms& u, bool radial) {
    float2 center = float2(u.params2.x * float(u.width - 1), u.params2.y * float(u.height - 1));
    float2 delta = float2(p) - center;
    float strength = clamp(u.params.y / 100.0, 0.0, 1.0);
    float4 sum = float4(0.0);
    for (int i = 0; i < 16; ++i) {
        float t = (float(i) / 15.0 - 0.5) * strength;
        float2 sample_position = float2(p) - delta * t;
        if (radial) {
            sample_position = center + float2(cos(t) * delta.x - sin(t) * delta.y,
                                               sin(t) * delta.x + cos(t) * delta.y);
        }
        sum += read_px(source, int2(round(sample_position)), u);
    }
    return sum / 16.0;
}

int depth_of_field_radius(texture2d<float, access::read> depth_tex, uint2 gid, constant Uniforms& u) {
    float depth = depth_tex.read(gid).r * 255.0;
    float focus = clamp(u.params.x, 0.0, 1.0) * 255.0;
    float aperture = clamp(u.params.y, 0.0, 1.0);
    float falloff = max(1.0, u.params.z);
    float max_radius = clamp(u.params.w, 1.0, 32.0);
    float amount = clamp(abs(depth - focus) / falloff, 0.0, 1.0) * aperture;
    return int(floor(amount * max_radius + 0.5001));
}

float4 edge_color(texture2d<float, access::read> source, int2 p, constant Uniforms& u) {
    float left = luma(read_px(source, p + int2(-1, 0), u).rgb);
    float right = luma(read_px(source, p + int2(1, 0), u).rgb);
    float up = luma(read_px(source, p + int2(0, -1), u).rgb);
    float down = luma(read_px(source, p + int2(0, 1), u).rgb);
    float edge = clamp(abs(right - left) + abs(down - up), 0.0, 1.0);
    return float4(float3(edge), read_px(source, p, u).a);
}

float curve_x_at(int index, constant Uniforms& u) {
    return index < 4 ? u.curve_x0[index] : u.curve_x1[index - 4];
}

float curve_y_at(int index, constant Uniforms& u) {
    return index < 4 ? u.curve_y0[index] : u.curve_y1[index - 4];
}

float curve_slope(int segment, constant Uniforms& u) {
    float width = max(0.001, curve_x_at(segment + 1, u) - curve_x_at(segment, u));
    return (curve_y_at(segment + 1, u) - curve_y_at(segment, u)) / width;
}

float curve_tangent(int point, int count, constant Uniforms& u) {
    if (point == 0) return curve_slope(0, u);
    if (point == count - 1) return curve_slope(count - 2, u);
    float left_slope = curve_slope(point - 1, u);
    float right_slope = curve_slope(point, u);
    if (left_slope * right_slope <= 0.0) return 0.0;
    float left_width = curve_x_at(point, u) - curve_x_at(point - 1, u);
    float right_width = curve_x_at(point + 1, u) - curve_x_at(point, u);
    float first_weight = 2.0 * right_width + left_width;
    float second_weight = right_width + 2.0 * left_width;
    return (first_weight + second_weight) /
           (first_weight / left_slope + second_weight / right_slope);
}

float evaluate_point_curve(float value, constant Uniforms& u) {
    float target = clamp(value, 0.0, 1.0);
    int count = clamp(int(u.curve_point_count), 2, 8);
    if (target <= curve_x_at(0, u)) {
        return clamp(curve_y_at(0, u), 0.0, 1.0);
    }
    for (int i = 1; i < 8; ++i) {
        if (i < count) {
            float x0 = clamp(curve_x_at(i - 1, u), 0.0, 1.0);
            float x1 = clamp(curve_x_at(i, u), x0 + 0.001, 1.0);
            if (target <= x1 || i == count - 1) {
                float y0 = clamp(curve_y_at(i - 1, u), 0.0, 1.0);
                float y1 = clamp(curve_y_at(i, u), 0.0, 1.0);
                float width = max(0.001, x1 - x0);
                float t = clamp((target - x0) / width, 0.0, 1.0);
                float t2 = t * t;
                float t3 = t2 * t;
                float left_tangent = curve_tangent(i - 1, count, u);
                float right_tangent = curve_tangent(i, count, u);
                return clamp((2.0 * t3 - 3.0 * t2 + 1.0) * y0 +
                             (t3 - 2.0 * t2 + t) * width * left_tangent +
                             (-2.0 * t3 + 3.0 * t2) * y1 +
                             (t3 - t2) * width * right_tangent, 0.0, 1.0);
            }
        }
    }
    return clamp(curve_y_at(count - 1, u), 0.0, 1.0);
}

kernel void pixelart_effect_kernel(texture2d<float, access::read> source [[texture(0)]],
                                   texture2d<float, access::read> mask_tex [[texture(1)]],
                                   texture2d<float, access::write> dest [[texture(2)]],
                                   texture2d<float, access::read> depth_tex [[texture(3)]],
                                   constant Uniforms& u [[buffer(0)]],
                                   uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.width || gid.y >= u.height) {
        return;
    }
    int2 p = int2(gid);
    float4 src = source.read(gid);
    if (mask_tex.read(gid).r < 0.5) {
        dest.write(src, gid);
        return;
    }

    float4 out = src;
    if (u.mode == 0) {
        float contrast = 1.0 + u.params.y * 2.0;
        out = float4(clamp((src.rgb - 0.5) * contrast + 0.5 + u.params.x, 0.0, 1.0), src.a);
    } else if (u.mode == 1) {
        float3 hsv = rgb_to_hsv(src.rgb);
        hsv.x = fract(hsv.x + u.params.x);
        hsv.y = clamp(hsv.y + u.params.y, 0.0, 1.0);
        hsv.z = clamp(hsv.z + u.params.z, 0.0, 1.0);
        out = float4(hsv_to_rgb(hsv), src.a);
    } else if (u.mode == 2) {
        float in_black = clamp(u.params.x, 0.0, 0.99);
        float in_white = max(in_black + 0.01, u.params.y);
        float3 normalized = clamp((src.rgb - in_black) / (in_white - in_black), 0.0, 1.0);
        normalized = pow(normalized, float3(1.0 / max(0.05, u.params.z)));
        out = float4(mix(float3(u.params.w), float3(u.params2.x), normalized), src.a);
    } else if (u.mode == 3 || u.mode == 4 || u.mode == 10) {
        float steps = max(2.0, u.params.x);
        float3 color = src.rgb;
        if (u.mode == 4) {
            color = clamp(color + (hash12(float2(gid)) - 0.5) / steps, 0.0, 1.0);
        }
        out = float4(floor(color * (steps - 1.0) + 0.5) / (steps - 1.0), src.a);
    } else if (u.mode == 5) {
        float lo = min(min(src.r, src.g), src.b);
        float hi = max(max(src.r, src.g), src.b);
        out = float4(clamp((src.rgb - lo) / max(0.01, hi - lo), 0.0, 1.0), src.a);
    } else if (u.mode == 6) {
        out = float4(float3(luma(src.rgb)), src.a);
    } else if (u.mode == 7) {
        out = float4(float3(dot(src.rgb, float3(0.393, 0.769, 0.189)),
                            dot(src.rgb, float3(0.349, 0.686, 0.168)),
                            dot(src.rgb, float3(0.272, 0.534, 0.131))), src.a);
    } else if (u.mode == 8) {
        out = float4(1.0 - src.rgb, src.a);
    } else if (u.mode == 9) {
        out = float4(src.rgb, 1.0 - src.a);
    } else if (u.mode == 11 || u.mode == 14) {
        out = blur_box(source, p, u, int(u.params.x));
    } else if (u.mode == 15) {
        out = motion_blur(source, p, u);
    } else if (u.mode == 16 || u.mode == 17) {
        out = radial_zoom_blur(source, p, u, u.mode == 16);
    } else if (u.mode == 12 || u.mode == 13 || u.mode == 39 || u.mode == 41) {
        float4 edge = edge_color(source, p, u);
        out = u.mode == 39 ? edge : float4(mix(src.rgb, edge.rgb, clamp(u.params.y / 100.0, 0.0, 1.0)), src.a);
    } else if (u.mode == 18 || u.mode == 29) {
        out = mix(src, blur_box(source, p, u, int(u.params.x)), 0.45);
    } else if (u.mode == 19) {
        float4 b = blur_box(source, p, u, int(u.params.x));
        out = length(src.rgb - b.rgb) < u.params.y / 255.0 ? b : src;
    } else if (u.mode == 20 || u.mode == 21) {
        int cell = max(1, int(u.params.x));
        int2 snapped = (p / cell) * cell + int2(cell / 2);
        out = read_px(source, snapped, u);
    } else if (u.mode == 22) {
        float radius = max(1.0, u.params.x);
        float2 jitter = float2(hash12(float2(gid)), hash12(float2(gid) + 19.7)) * 2.0 - 1.0;
        out = read_px(source, p + int2(jitter * radius), u);
    } else if (u.mode == 28) {
        float n = hash12(float2(gid) + u.params.x);
        if (n <= clamp(u.params.y / 100.0, 0.0, 1.0)) {
            float3 noise = mix(float3(n), float3(hash12(float2(gid) + 2.0), hash12(float2(gid) + 7.0), hash12(float2(gid) + 13.0)), clamp(u.params.z / 100.0, 0.0, 1.0));
            out = float4(mix(src.rgb, noise, clamp(u.params.x / 255.0, 0.0, 1.0)), src.a);
        }
    } else if (u.mode == 30) {
        float4 b = blur_box(source, p, u, int(u.params.x));
        out = float4(clamp(src.rgb + b.rgb * clamp(u.params.y / 100.0, 0.0, 1.0), 0.0, 1.0), src.a);
    } else if (u.mode == 31) {
        float3 hsv = rgb_to_hsv(src.rgb);
        if (hsv.x < 0.06 || hsv.x > 0.94) {
            hsv.y *= 1.0 - clamp(u.params.y / 100.0, 0.0, 1.0);
        }
        out = float4(hsv_to_rgb(hsv), src.a);
    } else if (u.mode == 32) {
        float4 sharp = src * 5.0 - read_px(source, p + int2(1, 0), u) - read_px(source, p - int2(1, 0), u) - read_px(source, p + int2(0, 1), u) - read_px(source, p - int2(0, 1), u);
        out = float4(mix(src.rgb, clamp(sharp.rgb, 0.0, 1.0), clamp(u.params.y / 100.0, 0.0, 1.0)), src.a);
    } else if (u.mode == 33) {
        float4 b = blur_box(source, p, u, int(u.params.x));
        out = float4(mix(src.rgb, b.rgb * float3(1.05, 1.0, 0.94), clamp(u.params.y / 100.0, 0.0, 1.0)), src.a);
    } else if (u.mode == 34) {
        float2 uv = float2(gid) / float2(max(1u, u.width), max(1u, u.height));
        float distance_from_center = distance(uv, clamp(u.params2.xy, 0.0, 1.0)) / max(0.1, u.params.x);
        float v = smoothstep(0.9, 0.2, distance_from_center + u.params.y / 400.0);
        out = float4(src.rgb * v, src.a);
    } else if (u.mode == 35) {
        float n = fbm(float2(gid) / max(1.0, u.params.x));
        out = float4(mix(u.primary.rgb, u.secondary.rgb, n), src.a);
    } else if (u.mode == 38) {
        float n = fbm(float2(gid) / max(1.0, u.params.x));
        out = float4(mix(src.rgb, float3(n), 0.65), src.a);
    } else if (u.mode == 40 || u.mode == 42) {
        float l0 = luma(read_px(source, p - int2(1, 1), u).rgb);
        float l1 = luma(read_px(source, p + int2(1, 1), u).rgb);
        out = float4(float3(clamp((l0 - l1) + 0.5, 0.0, 1.0)), src.a);
    } else if (u.mode == 44) {
        float white_point = clamp(u.params.x, -1.0, 1.0);
        float highlights = clamp(u.params.y, -1.0, 1.0);
        float shadows = clamp(u.params.z, -1.0, 1.0);
        float black_point = clamp(u.params.w, -1.0, 1.0);
        float black_anchor = clamp(black_point * 0.20, -0.20, 0.20);
        float white_anchor = clamp(1.0 - white_point * 0.20, 0.80, 1.20);
        float3 color = clamp((src.rgb - float3(black_anchor)) / max(0.05, white_anchor - black_anchor), 0.0, 1.0);
        float lum = clamp(luma(src.rgb), 0.0, 1.0);
        float shadow_weight = pow(1.0 - lum, 1.6);
        float highlight_weight = pow(lum, 1.6);
        color += shadows * 0.45 * shadow_weight * (shadows >= 0.0 ? (float3(1.0) - color) : color);
        color += highlights * 0.45 * highlight_weight * (highlights >= 0.0 ? (float3(1.0) - color) : color);
        out = float4(clamp(color, 0.0, 1.0), src.a);
    } else if (u.mode == 45) {
        float3 color = src.rgb;
        if (u.params2.x > 0.5) {
            float lum = clamp(luma(color), 0.0, 1.0);
            float mapped_lum = evaluate_point_curve(lum, u);
            if (lum > 0.0001) {
                color *= mapped_lum / lum;
            } else {
                color = float3(mapped_lum);
            }
        } else {
            if (u.params2.y > 0.5) color.r = evaluate_point_curve(color.r, u);
            if (u.params2.z > 0.5) color.g = evaluate_point_curve(color.g, u);
            if (u.params2.w > 0.5) color.b = evaluate_point_curve(color.b, u);
        }
        out = float4(clamp(color, 0.0, 1.0), src.a);
    } else if (u.mode == 46) {
        float temperature = clamp(u.params.x, -1.0, 1.0);
        float warm = max(temperature, 0.0);
        float cool = max(-temperature, 0.0);
        float3 offset = float3(warm * 45.0 - cool * 28.0,
                               warm * 12.0 + cool * 8.0,
                               -warm * 35.0 + cool * 45.0) / 255.0;
        out = float4(clamp(src.rgb + offset, 0.0, 1.0), src.a);
    } else if (u.mode == 47) {
        int radius = depth_of_field_radius(depth_tex, gid, u);
        out = radius <= 0 ? src : blur_box(source, p, u, radius);
    } else {
        float2 uv = float2(gid) / float2(max(1u, u.width), max(1u, u.height));
        float2 center = (u.mode == 23 || u.mode == 24) ? clamp(u.params2.xy, 0.0, 1.0) : float2(0.5);
        float2 delta = uv - center;
        if (u.mode == 23) {
            float dist = length(delta);
            float2 sample_uv = center + delta * (1.0 - u.params.x * 0.35 * (1.0 - dist));
            out = read_px(source, int2(sample_uv * float2(u.width, u.height)), u);
        } else if (u.mode == 24) {
            float radius = length(delta) / max(0.05, u.params.y * 0.75);
            float angle = u.params.x * 6.28318 * (1.0 - clamp(radius, 0.0, 1.0));
            float2 sample_uv = center + float2(cos(angle) * delta.x - sin(angle) * delta.y, sin(angle) * delta.x + cos(angle) * delta.y);
            out = read_px(source, int2(sample_uv * float2(u.width, u.height)), u);
        } else if (u.mode == 27) {
            float radius = length(delta);
            float angle = atan2(delta.y, delta.x);
            float2 sample_uv = center + float2(cos(angle), sin(angle)) * (1.0 - radius) * u.params.x;
            out = read_px(source, int2(sample_uv * float2(u.width, u.height)), u);
        } else if (u.mode == 36 || u.mode == 37) {
            float2 z = (uv - 0.5) * max(0.1, 3.0 / max(0.1, u.params.x));
            float2 c = u.mode == 36 ? float2(-0.8, 0.156) : z;
            float iter = 0.0;
            for (int i = 0; i < 64; ++i) {
                z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
                if (dot(z, z) > 4.0) break;
                iter += 1.0;
            }
            out = float4(0.5 + 0.5 * cos(float3(0.0, 2.0, 4.0) + iter * 0.18), src.a);
        } else if (u.mode == 25 || u.mode == 26) {
            out = blur_box(source, p, u, max(1, int(u.params.x)));
        }
    }
    dest.write(clamp(out, 0.0, 1.0), gid);
}
)MSL";

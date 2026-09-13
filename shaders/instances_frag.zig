const std = @import("std");
const math = std.math;

const za = @import("zalgebra");
const Vec2f = za.Vec2;
const Vec3f = za.Vec3;
const Vec4f = za.Vec4;
const Mat3f = za.Mat3;
const Mat4f = za.Mat4;

const common = @import("common.zig");
const Image2D = common.Image2D;
const uniformConstant = common.uniformConstant;
const uniform = common.uniform;
const input = common.input;
const output = common.output;

const PointLight = extern struct {
    position: [3]f32,
    falloff_cm: f32,
    color: [3]f32,
    color_intensity_lumens: f32,
};

const Constants = extern struct {
    mat_proj: Mat4f,
    mat_view: Mat4f,
    mat_model: Mat4f,
    view_position: [3]f32,
    num_lights: u32,
    _: Vec4f,
    lights: [3]PointLight,
};

comptime {
    const assert = std.debug.assert;
    assert(@sizeOf(PointLight) == 0x20);
    assert(@sizeOf(Constants) == 0x140);
}

// HACK: psbc needs the binding to be manually specified in order
// to generate correct resource offsets for now
const c = uniform(Constants, "c", .{ .descriptor = .{ .set = 0, .binding = 12 } });

const texture = uniformConstant(common.Image2D(f32), "tex", .{ .descriptor = .{ .set = 0, .binding = 0 } });
const sampler = uniformConstant(common.Sampler, "sampler", .{ .descriptor = .{ .set = 0, .binding = 1 } });

const in_normal = input(Vec3f, "in_normal", .{ .location = 0 });
const in_uv = input(Vec2f, "in_uv", .{ .location = 1 });
const in_view_pos = input(Vec3f, "in_view_pos", .{ .location = 2 });
const in_metal_rough = input(Vec2f, "in_metal_rough", .{ .location = 3 });
const out_color = output(Vec4f, "out_color", .{ .location = 0 });

export fn main() callconv(.{ .spirv_fragment = .{} }) void {
    const albedo = (Vec4f{ .data = common.sampleImplicitLod2Df(texture, sampler, in_uv.data) }).toVec3();

    const view_pos: Vec3f = .{ .data = c.view_position };
    const V = view_pos.sub(in_view_pos.*).norm();
    const N = in_normal.norm();

    const metallic = in_metal_rough.x();
    const roughness = in_metal_rough.y();

    const F0 = Vec3f.set(0.04).lerp(albedo, metallic);

    // specular + diffuse BRDF
    var Lo: Vec3f = .zero();
    Lo = Lo.add(BRDF(V, N, in_view_pos.*, albedo, F0, metallic, roughness, c.lights[0]));
    Lo = Lo.add(BRDF(V, N, in_view_pos.*, albedo, F0, metallic, roughness, c.lights[1]));

    // diffuse
    const ambient = albedo.mul(.set(0.02));
    const color = ambient.add(Lo);

    // Gamma correct
    const correct_color: Vec3f = .{ .data = common.linearToSrgb(color.data) };

    out_color.* = .fromVec3(correct_color, 1.0);
}

fn D_GGX(NoH: f32, roughness: f32) f32 {
    const a = roughness * roughness;
    const a2 = a * a;
    const f = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (math.pi * f * f);
}

fn V_SchlicksmithGGX(NoL: f32, NoV: f32, roughness: f32) f32 {
    const r = roughness + 1.0;
    const k = (r * r) / 8.0;
    const GL = NoL / (NoL * (1.0 - k) + k);
    const GV = NoV / (NoV * (1.0 - k) + k);
    return GL * GV;
}

fn F_Schlick(f0: Vec3f, u: f32) Vec3f {
    // FIXME: Zig's SPIR-V backend can't emit pow() instructions,
    // workaround by manually multiplying manually.
    // const f = math.pow(f32, 1.0 - u, 5.0);
    const f_first = 1.0 - u;
    const f = f_first * f_first * f_first * f_first * f_first;
    return f0.mul(.set(1.0 - f)).add(.set(f));
}

fn Fd_Lambert() f32 {
    return 1.0 / math.pi;
}

fn specularBRDF(NoH: f32, NoL: f32, NoV: f32, f0: Vec3f, roughness: f32) Vec3f {
    const D = D_GGX(NoH, roughness);
    const G = V_SchlicksmithGGX(NoL, NoV, roughness);
    const F = F_Schlick(f0, NoV);

    return Vec3f.set(D).mul(.set(G)).mul(F).div(.set(4.0 * NoL * NoV));
}

fn diffuseBRDF(albedo_color: Vec3f) Vec3f {
    return albedo_color.mul(.set(Fd_Lambert()));
}

// attenuation from frostbite
fn smoothDistanceAtt(squared_distance: f32, inv_sqr_att_radius: f32) f32 {
    const factor = squared_distance * inv_sqr_att_radius;
    const smooth_factor = math.clamp(1.0 - factor * factor, 0.0, 1.0);
    return smooth_factor * smooth_factor;
}

fn frostbiteDistanceAttenuation(unorm_light_vector: Vec3f, inv_sqr_att_radius: f32) f32 {
    const sqr_dist = unorm_light_vector.lengthSq();

    var attenuation = 1.0 / @max(sqr_dist, 0.01 * 0.01);
    attenuation *= smoothDistanceAtt(sqr_dist, inv_sqr_att_radius);

    return attenuation;
}

fn BRDF(V: Vec3f, N: Vec3f, worldPos: Vec3f, fragColor: Vec3f, f0: Vec3f, metallic: f32, roughness: f32, light: PointLight) Vec3f {
    const light_pos: Vec3f = .{ .data = light.position };
    const light_world_unorm = light_pos.sub(worldPos);
    const L = light_world_unorm.norm();

    const H = V.add(L).norm();
    const NoV = math.clamp(N.dot(V), 0.0, 1.0);
    const NoL = math.clamp(N.dot(L), 0.0, 1.0);
    const NoH = math.clamp(N.dot(H), 0.0, 1.0);
    // const LoH = math.clamp(L.dot(H), 0.0, 1.0);

    const attenuation =
        frostbiteDistanceAttenuation(light_world_unorm, light.falloff_cm);

    if (NoL == 0.0 or attenuation == 0.0) {
        return .zero();
    }

    const color_intensity = light.color_intensity_lumens;

    const F = F_Schlick(f0, NoV);

    var kD = Vec3f.set(1.0).sub(F);
    kD = kD.mul(Vec3f.set(1.0 - metallic));

    const specular = specularBRDF(NoH, NoL, NoV, f0, roughness);
    const diffuse = diffuseBRDF(kD.mul(fragColor));

    const light_color: Vec3f = .{ .data = light.color };
    const color = specular.add(diffuse);
    return color.mul(light_color).mul(.set(color_intensity * attenuation * NoL));
}

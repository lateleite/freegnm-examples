const std = @import("std");
const gpu = std.spirv;

const za = @import("zalgebra");
const Vec2f = za.Vec2;
const Vec3f = za.Vec3;
const Vec4f = za.Vec4;
const Mat3f = za.Mat3;
const Mat4f = za.Mat4;

const common = @import("common.zig");
const uniform = common.uniform;
const input = common.input;
const output = common.output;

const Constants = extern struct {
    mat_proj: Mat4f,
    mat_view: Mat4f,
    mat_model: Mat4f,
    view_position: Vec4f,
};

comptime {
    const assert = std.debug.assert;
    assert(@sizeOf(Constants) == 0xd0);
}

const c = uniform(Constants, "c", .{ .descriptor = .{ .set = 0, .binding = 0 } });

const in_pos = input(Vec3f, "in_pos", .{ .location = 0 });
const in_normal = input(Vec3f, "in_normal", .{ .location = 1 });
const in_uv = input(Vec2f, "in_uv", .{ .location = 2 });
const in_metal_rough = input(Vec2f, "in_metal_rough", .{ .location = 3 });
const out_normal = output(Vec3f, "out_normal", .{ .location = 0 });
const out_uv = output(Vec2f, "out_uv", .{ .location = 1 });
const out_view_pos = output(Vec3f, "out_view_pos", .{ .location = 2 });
const out_metal_rough = output(Vec2f, "out_metal_rough", .{ .location = 3 });

export fn main() callconv(.spirv_vertex) void {
    const world_pos = c.mat_model.mulByVec4(.fromVec3(in_pos.*, 1.0));

    gpu.position_out.* = c.mat_proj.mulByVec4(c.mat_view.mulByVec4(world_pos)).data;

    out_normal.* = c.mat_model.mulByVec3(in_normal.*);
    out_uv.* = in_uv.*;
    out_view_pos.* = world_pos.toVec3();
    out_metal_rough.* = in_metal_rough.*;
}

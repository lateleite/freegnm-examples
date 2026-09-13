const std = @import("std");
const gpu = std.spirv;

const za = @import("zalgebra");
const Vec3f = za.Vec3;
const Mat4f = za.Mat4;

const common = @import("common.zig");
const Vec2f = common.Vec2f;
const Vec4f = common.Vec4f;
const uniform = common.uniform;
const input = common.input;
const output = common.output;

const Constants = extern struct {
    mvp: Mat4f,
};

comptime {
    const assert = std.debug.assert;
    assert(@sizeOf(Constants) == 0x40);
}

const c = uniform(Constants, "c", .{ .descriptor = .{ .set = 0, .binding = 0 } });

const in_pos = input(Vec3f, "in_pos", .{ .location = 0 });
const in_uv = input(Vec2f, "in_uv", .{ .location = 1 });
const out_uv = output(Vec2f, "out_uv", .{ .location = 0 });

export fn main() callconv(.spirv_vertex) void {
    gpu.position_out.* = c.mvp.mulByVec4(.fromVec3(in_pos.*, 1.0)).data;
    out_uv.* = in_uv.*;
}

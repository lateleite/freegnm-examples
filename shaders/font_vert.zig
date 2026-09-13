const std = @import("std");
const gpu = std.spirv;

const za = @import("zalgebra");
const Vec2f = za.Vec2;
const Mat4f = za.Mat4;

const common = @import("common.zig");
const Vec4f = common.Vec4f;
const uniform = common.uniform;
const input = common.input;
const output = common.output;

const Constants = extern struct {
    mat_proj: Mat4f,
};

comptime {
    const assert = std.debug.assert;
    assert(@sizeOf(Constants) == 0x40);
}

const c = uniform(Constants, "c", .{ .descriptor = .{ .set = 0, .binding = 0 } });

const in_pos = input(Vec2f, "in_pos", .{ .location = 0 });
const in_uv = input(Vec2f, "in_uv", .{ .location = 1 });
const in_color = input(Vec4f, "in_color", .{ .location = 2 });
const out_uv = output(Vec2f, "out_uv", .{ .location = 0 });
const out_color = output(Vec4f, "out_color", .{ .location = 1 });

export fn main() callconv(.spirv_vertex) void {
    gpu.position_out.* = c.mat_proj.mulByVec4(.fromVec2(in_pos.*, 0.0, 1.0)).data;
    out_uv.* = in_uv.*;
    out_color.* = in_color.*;
}

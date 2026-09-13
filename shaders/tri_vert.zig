const std = @import("std");
const gpu = std.spirv;

const common = @import("common.zig");
const Vec4f = common.Vec4f;
const output = common.output;

extern var vertex_index: u32 addrspace(.vertex_index);

const out_color = output(Vec4f, "out_color", .{ .location = 0 });

export fn main() callconv(.spirv_vertex) void {
    var x: f32 = undefined;
    var y: f32 = undefined;
    var red: f32 = undefined;

    switch (gpu.vertex_index) {
        0 => {
            x = -0.5;
            y = -0.4;
            red = 0.1;
        },
        1 => {
            x = 0;
            y = 0.6;
            red = 0.5;
        },
        else => {
            x = 0.5;
            y = -0.4;
            red = 0.9;
        },
    }

    gpu.position_out.* = .{ x, y, 0.0, 1.0 };
    out_color.* = .{ 1.0, red, 0.0, 1.0 };
}

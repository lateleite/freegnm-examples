const common = @import("common.zig");
const Vec4f = common.Vec4f;
const input = common.input;
const output = common.output;

const in_color = input(Vec4f, "in_color", .{ .location = 0 });
const out_color = output(Vec4f, "out_color", .{ .location = 0 });

export fn main() callconv(.{ .spirv_fragment = .{} }) void {
    out_color.* = in_color.*;
}

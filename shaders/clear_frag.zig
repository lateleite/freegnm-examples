const common = @import("common.zig");
const Vec4f = common.Vec4f;
const uniform = common.uniform;
const output = common.output;

const Constants = extern struct {
    color: Vec4f,
};

const c = uniform(Constants, "c", .{ .descriptor = .{ .set = 0, .binding = 0 } });
const out_color = output(Vec4f, "out_color", .{ .location = 0 });

export fn main() callconv(.{ .spirv_fragment = .{} }) void {
    out_color.* = c.color;
}

const common = @import("common.zig");
const Vec2f = common.Vec2f;
const Vec4f = common.Vec4f;
const Image2D = common.Image2D;
const uniformConstant = common.uniformConstant;
const input = common.input;
const output = common.output;

const texture = uniformConstant(common.Image2D(f32), "tex", .{ .descriptor = .{ .set = 0, .binding = 0 } });
const sampler = uniformConstant(common.Sampler, "sampler", .{ .descriptor = .{ .set = 0, .binding = 1 } });

const in_uv = input(Vec2f, "in_uv", .{ .location = 0 });
const out_color = output(Vec4f, "out_color", .{ .location = 0 });

export fn main() callconv(.{ .spirv_fragment = .{} }) void {
    const color = common.sampleImplicitLod2Df(texture, sampler, in_uv.*);
    out_color.* = .{ color[0], color[1], color[2], 1.0 };
}

const std = @import("std");
const orbis = @import("open_orbis_zig");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{
        .default_target = .{
            .cpu_arch = .x86_64,
            .os_tag = .ps4,
        },
    });
    const optimize = b.standardOptimizeOption(.{});

    if (target.result.os.tag != .ps4) {
        @panic(
            \\PS4 is the only supported target, please set it as your build target.
            \\For example: zig build -Dtarget=x86_64-ps4
            \\
        );
    }

    const native_target = b.graph.host;

    //
    // build options,
    // useful for capturing screenshots used in emulator comparison tests
    //
    const build_options = b.addOptions();
    build_options.addOption(?usize, "capture_frame_index", b.option(
        usize,
        "capture-frame-index",
        "Which frame should be captured to a screenshot?",
    ));
    build_options.addOption([]const u8, "capture_frame_path", b.option(
        []const u8,
        "capture-frame-path",
        "Where should the captured frame be saved to?",
    ) orelse "/data/capture.qoi");
    build_options.addOption(?usize, "max_frames_count", b.option(
        usize,
        "max-frames-count",
        "Which frame should be captured to a screenshot?",
    ));

    //
    // dependencies
    //
    const dep_cglm = b.dependency("cglm", .{});

    const dep_gnm = b.dependency("freegnm", .{
        .target = target,
        .optimize = optimize,
        .pic = true,
    });
    const mod_gnm = dep_gnm.module("gnm");
    const lib_gnm = dep_gnm.artifact("gnm");

    const dep_orbis = b.dependency("open_orbis_zig", .{
        .target = target,
        .optimize = if (optimize == .fast) std.lang.Optimize.fast else std.lang.Optimize.safe,
        .pic = true,
    });
    // const mod_orbis = dep_orbis.module("orbis");
    const lib_musl = dep_orbis.artifact("musl");
    const wf_syslibs = dep_orbis.namedWriteFiles("sys_libs");

    const dep_psbc = b.dependency("psbc", .{
        .target = native_target,
        .optimize = .fast,
    });
    const exe_psbc = dep_psbc.artifact("psbc");

    // OpenOrbis/musl needs libkernel
    lib_musl.root_module.addLibraryPath(wf_syslibs.getDirectory());
    lib_musl.root_module.linkSystemLibrary("kernel", .{});
    // freegnm needs libc (for now), so link OpenOrbis-musl to it
    lib_gnm.root_module.addLibraryPath(wf_syslibs.getDirectory());
    mod_gnm.linkLibrary(lib_musl);

    //
    // compile shaders
    //
    const sh_tri_vert = compilePsb(b, exe_psbc, "tri", .vertex);
    const sh_tri_frag = compilePsb(b, exe_psbc, "tri", .fragment);
    const sh_cube_vert = compilePsb(b, exe_psbc, "cube", .vertex);
    const sh_cube_frag = compilePsb(b, exe_psbc, "cube", .fragment);
    const sh_gltf_vert = compilePsb(b, exe_psbc, "gltf", .vertex);
    const sh_gltf_frag = compilePsb(b, exe_psbc, "gltf", .fragment);
    const sh_instances_vert = compilePsb(b, exe_psbc, "instances", .vertex);
    const sh_instances_frag = compilePsb(b, exe_psbc, "instances", .fragment);
    const sh_indirect_vert = compilePsb(b, exe_psbc, "indirect", .vertex);
    const sh_indirect_frag = compilePsb(b, exe_psbc, "indirect", .fragment);
    const sh_font_vert = compilePsb(b, exe_psbc, "font", .vertex);
    const sh_font_frag = compilePsb(b, exe_psbc, "font", .fragment);
    const sh_clear_frag = compilePsb(b, exe_psbc, "clear", .fragment);

    //
    // adding all samples
    //

    // triangle
    const exe_triangle = b.addExecutable(.{
        .name = "triangle",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = false,
        }),
        .use_llvm = true,
        .use_lld = true,
    });
    exe_triangle.root_module.addCSourceFiles(.{
        .root = b.path("triangle"),
        .files = &.{
            "src/main.c",
        },
        .flags = C_FLAGS,
        .language = .c,
    });
    const assets_triangle: []const orbis.Asset = &.{
        .{ .source = sh_tri_vert, .target_path = "shaders/tri_vert.sb" },
        .{ .source = sh_tri_frag, .target_path = "shaders/tri_frag.sb" },
        .{ .source = sh_clear_frag, .target_path = "shaders/clear_frag.sb" },
    };

    // cube
    const exe_cube = b.addExecutable(.{
        .name = "cube",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = false,
        }),
        .use_llvm = true,
        .use_lld = true,
    });
    exe_cube.root_module.addCSourceFiles(.{
        .root = b.path("cube"),
        .files = &.{
            "src/main.c",
        },
        .flags = C_FLAGS,
        .language = .c,
    });
    const assets_cube: []const orbis.Asset = &.{
        .{ .source = sh_cube_vert, .target_path = "shaders/cube_vert.sb" },
        .{ .source = sh_cube_frag, .target_path = "shaders/cube_frag.sb" },
        .{ .source = sh_clear_frag, .target_path = "shaders/clear_frag.sb" },
        .{ .source = b.path("assets/images/cube_face.gnf"), .target_path = "images/cube_face.gnf" },
    };

    // gltf
    const exe_gltf = b.addExecutable(.{
        .name = "gltf",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = false,
        }),
        .use_llvm = true,
        .use_lld = true,
    });
    exe_gltf.root_module.addCSourceFiles(.{
        .root = b.path("gltf"),
        .files = &.{
            "src/main.c",
            "src/pass_model.c",
            "src/pass_ui.c",
        },
        .flags = C_FLAGS,
        .language = .c,
    });

    const assets_gltf: []const orbis.Asset = &.{
        .{ .source = sh_gltf_vert, .target_path = "shaders/gltf_vert.sb" },
        .{ .source = sh_gltf_frag, .target_path = "shaders/gltf_frag.sb" },
        .{ .source = sh_font_vert, .target_path = "shaders/font_vert.sb" },
        .{ .source = sh_font_frag, .target_path = "shaders/font_frag.sb" },
        .{ .source = sh_clear_frag, .target_path = "shaders/clear_frag.sb" },
        .{ .source = b.path("assets/fonts/Hack-Regular.ttf"), .target_path = "fonts/Hack-Regular.ttf" },
        .{ .source = b.path("assets/models/chinesedragon.gltf"), .target_path = "models/chinesedragon.gltf" },
    };

    // instances
    const exe_instances = b.addExecutable(.{
        .name = "instances",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = false,
        }),
        .use_llvm = true,
        .use_lld = true,
    });
    exe_instances.root_module.addCSourceFiles(.{
        .root = b.path("instances"),
        .files = &.{
            "src/main.c",
            "src/pass_model.c",
            "src/pass_ui.c",
        },
        .flags = C_FLAGS,
        .language = .c,
    });
    const assets_instances: []const orbis.Asset = &.{
        .{ .source = sh_instances_vert, .target_path = "shaders/instances_vert.sb" },
        .{ .source = sh_instances_frag, .target_path = "shaders/instances_frag.sb" },
        .{ .source = sh_font_vert, .target_path = "shaders/font_vert.sb" },
        .{ .source = sh_font_frag, .target_path = "shaders/font_frag.sb" },
        .{ .source = sh_clear_frag, .target_path = "shaders/clear_frag.sb" },
        .{ .source = b.path("assets/fonts/Hack-Regular.ttf"), .target_path = "fonts/Hack-Regular.ttf" },
        .{ .source = b.path("assets/models/fruit_of_knowledge.gltf"), .target_path = "models/fruit_of_knowledge.gltf" },
    };

    // indirect
    const exe_indirect = b.addExecutable(.{
        .name = "indirect",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = false,
        }),
        .use_llvm = true,
        .use_lld = true,
    });
    exe_indirect.root_module.addCSourceFiles(.{
        .root = b.path("indirect"),
        .files = &.{
            "src/main.c",
            "src/pass_model.c",
            "src/pass_ui.c",
        },
        .flags = C_FLAGS,
        .language = .c,
    });
    const assets_indirect: []const orbis.Asset = &.{
        .{ .source = sh_indirect_vert, .target_path = "shaders/indirect_vert.sb" },
        .{ .source = sh_indirect_frag, .target_path = "shaders/indirect_frag.sb" },
        .{ .source = sh_font_vert, .target_path = "shaders/font_vert.sb" },
        .{ .source = sh_font_frag, .target_path = "shaders/font_frag.sb" },
        .{ .source = sh_clear_frag, .target_path = "shaders/clear_frag.sb" },
        .{ .source = b.path("assets/fonts/Hack-Regular.ttf"), .target_path = "fonts/Hack-Regular.ttf" },
        .{ .source = b.path("assets/models/venus.gltf"), .target_path = "models/venus.gltf" },
    };

    const ctx: BuildSampleContext = .{
        .b = b,
        .dep_cglm = dep_cglm,
        .dep_orbis = dep_orbis,
        .lib_musl = lib_musl,
        .lib_gnm = lib_gnm,
    };

    try buildSample(ctx, exe_triangle, b.path("triangle"), "IV0000-FGNM00000_00-TRIANGLESAMPLE00", assets_triangle);
    try buildSample(ctx, exe_cube, b.path("cube"), "IV0000-FGNM00001_00-CUBESAMPLE000000", assets_cube);
    try buildSample(ctx, exe_gltf, b.path("gltf"), "IV0000-FGNM00002_00-GLTFSAMPLE000000", assets_gltf);
    try buildSample(ctx, exe_instances, b.path("instances"), "IV0000-FGNM00004_00-INSTANCES0000000", assets_instances);
    try buildSample(ctx, exe_indirect, b.path("indirect"), "IV0000-FGNM00005_00-INDIRECT00000000", assets_indirect);

    //
    // check stage for ZLS
    //
    const step_check = b.step("check", "Check if the project compiles");

    const check_tri = b.addExecutable(.{
        .name = "tri",
        .root_module = exe_triangle.root_module,
    });
    step_check.dependOn(&check_tri.step);
    const check_cube = b.addExecutable(.{
        .name = "cube",
        .root_module = exe_cube.root_module,
    });
    step_check.dependOn(&check_cube.step);
}

const C_FLAGS: []const []const u8 = &.{
    "-std=gnu11",
    // required to stop clang from trying to link the official SDK's libSceDbgUBSanitizer_stub_weak.a
    "-nostdlib",
    "-nodefaultlibs",
    // cglm defines
    // it needs CGLM_ALL_UNALIGNED because buffers that use its objects may not be aligned
    "-DCGLM_ALL_UNALIGNED",
    "-DCGLM_FORCE_LEFT_HANDED",
    "-DCGLM_FORCE_DEPTH_ZERO_TO_ONE",
};

const BuildSampleContext = struct {
    b: *std.Build,
    dep_cglm: *std.Build.Dependency,
    dep_orbis: *std.Build.Dependency,
    lib_musl: *std.Build.Step.Compile,
    lib_gnm: *std.Build.Step.Compile,
};

fn buildSample(
    ctx: BuildSampleContext,
    exe: *std.Build.Step.Compile,
    base_path: std.Build.LazyPath,
    content_id: []const u8,
    pkg_assets: []const orbis.Asset,
) !void {
    const b = ctx.b;
    const dep_cglm = ctx.dep_cglm;
    const dep_orbis = ctx.dep_orbis;
    const lib_musl = ctx.lib_musl;
    const lib_gnm = ctx.lib_gnm;

    // add program
    exe.root_module.addCSourceFiles(.{
        .root = b.path("shared"),
        .files = &.{
            "src/u/fs.c",
            "src/u/utility.c",
            "src/displayctx.c",
            "src/font2d.c",
            "src/gltfmodel.c",
            "src/memalloc.c",
            "src/misc.c",
        },
        .flags = C_FLAGS,
        .language = .c,
    });

    exe.root_module.addIncludePath(dep_cglm.path("include"));
    exe.root_module.addIncludePath(b.path("shared/src"));
    exe.root_module.linkLibrary(lib_gnm);

    // add OpenOrbis include and library paths
    exe.pie = true;
    exe.link_eh_frame_hdr = true;
    exe.link_emit_relocs = true;

    // add OpenOrbis' libc
    exe.root_module.linkLibrary(lib_musl);
    exe.setLinkerScript(dep_orbis.path("link.x"));

    // system libraries and freegnm
    const wf_syslibs = dep_orbis.namedWriteFiles("sys_libs");
    exe.root_module.addLibraryPath(wf_syslibs.getDirectory());
    exe.root_module.linkSystemLibrary("kernel", .{});
    exe.root_module.linkSystemLibrary("SceGnmDriver", .{});
    exe.root_module.linkSystemLibrary("SceVideoOut", .{});

    //
    // create fakeself eboot.bin out of new executable
    //
    const eboot_file = orbis.createFself(b, exe, b.fmt("{s}_eboot.bin", .{exe.name}), .{});

    //
    // create param.sfo describing program's package
    //
    const sfo_file = orbis.createSfo(b, dep_orbis, base_path.path(b, "param.sfo.zon"), "param.sfo");

    //
    // generate pkg.gp4
    //
    const BASE_ASSETS: []const orbis.Asset = &.{
        .{ .source = eboot_file, .target_path = "eboot.bin" },
        .{ .source = sfo_file, .target_path = "sce_sys/param.sfo" },
        .{ .source = b.path("assets/sce_module/libSceFios2.prx"), .target_path = "sce_module/libSceFios2.prx" },
        .{ .source = b.path("assets/sce_module/libc.prx"), .target_path = "sce_module/libc.prx" },
        .{ .source = b.path("assets/sce_sys/about/right.sprx"), .target_path = "sce_sys/about/right.sprx" },
        .{ .source = b.path("assets/sce_sys/icon0.png"), .target_path = "sce_sys/icon0.png" },
    };

    const all_assets = try b.allocator.alloc(orbis.Asset, BASE_ASSETS.len + pkg_assets.len);
    @memcpy(all_assets[0..BASE_ASSETS.len], BASE_ASSETS);
    @memcpy(all_assets[BASE_ASSETS.len..], pkg_assets);

    const gp4_file = orbis.createGp4(b, dep_orbis, content_id, all_assets, "pkg.gp4");

    //
    // build the PKG itself
    // depends on eboot.bin, param.sfo, pkg.gp4 and other assets
    //
    const wf_pkg = orbis.setupPkg(b, all_assets);

    // HACK: LibOrbisPkg needs the gp4 file to be with all other assets,
    // else it won't be able to find the asset files.
    const installed_gp4_file = wf_pkg.addCopyFile(gp4_file, "pkg.gp4");

    const pkg_cmd = orbis.createPkg(b, wf_pkg, installed_gp4_file);

    //
    // install PKG
    //
    const plg_name = b.fmt("{s}.pkg", .{content_id});
    const install_pkg = b.addInstallFile(wf_pkg.getDirectory().path(b, plg_name), plg_name);
    // depend on the pkg itself
    install_pkg.step.dependOn(&pkg_cmd.step);
    // add PKG installation to install step
    b.getInstallStep().dependOn(&install_pkg.step);

    // add eboot.bin to install step,
    // useful for emulators
    const install_eboot = b.addInstallFile(eboot_file, b.fmt("{s}_eboot.bin", .{exe.name}));
    install_eboot.step.dependOn(&pkg_cmd.step);
    b.getInstallStep().dependOn(&install_eboot.step);
}

const ShaderType = enum {
    vertex,
    fragment,
};

fn compilePsb(
    b: *std.Build,
    exe_psbc: *std.Build.Step.Compile,
    name: []const u8,
    sh_type: ShaderType,
) std.Build.LazyPath {
    const stage_name = switch (sh_type) {
        .vertex => "vertex",
        .fragment => "fragment",
    };
    const file_suffix = switch (sh_type) {
        .vertex => "vert",
        .fragment => "frag",
    };

    const vulkan12_target = b.resolveTargetQuery(.{
        .cpu_arch = .spirv32,
        .cpu_model = .{ .explicit = &std.Target.spirv.cpu.vulkan_v1_2 },
        .os_tag = .vulkan,
        .ofmt = .spirv,
    });

    const dep_zalgebra = b.dependency("zalgebra", .{
        .target = vulkan12_target,
        .optimize = .fast,
    });

    const shader = b.addObject(.{
        .name = b.fmt("shader.{s}", .{file_suffix}),
        .root_module = b.createModule(.{
            .root_source_file = b.path(b.fmt("shaders/{s}_{s}.zig", .{ name, file_suffix })),
            .imports = &.{
                .{ .name = "zalgebra", .module = dep_zalgebra.module("zalgebra") },
            },
            .target = vulkan12_target,
            .optimize = .fast,
        }),
        .use_llvm = false,
        .use_lld = false,
    });

    const psb_cmd = b.addRunFile(exe_psbc.getEmittedBin());
    psb_cmd.addArgs(&.{ "-s", stage_name });
    psb_cmd.addFileArg(shader.getEmittedBin());

    const sb_file = psb_cmd.addOutputFileArg2(b.fmt("{s}_{s}.sb", .{ name, file_suffix }), .{});

    // add final shader file for debugging purposes
    const install_sb = b.addInstallFile(sb_file, b.fmt("shaders/{s}_{s}.sb", .{ name, file_suffix }));
    install_sb.step.dependOn(&psb_cmd.step);
    b.getInstallStep().dependOn(&install_sb.step);
    const install_spv = b.addInstallFile(shader.getEmittedBin(), b.fmt("shaders/{s}_{s}.spv", .{ name, file_suffix }));
    install_spv.step.dependOn(&shader.step);
    b.getInstallStep().dependOn(&install_spv.step);

    return sb_file;
}

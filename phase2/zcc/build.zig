const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "zcc",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run zcc");
    run_step.dependOn(&run_cmd.step);

    const smoke_cmd = b.addRunArtifact(exe);
    smoke_cmd.addArgs(&.{ "-E", "tests/smoke/hello.c" });

    const smoke_step = b.step("smoke", "Run zcc smoke tests");
    smoke_step.dependOn(&smoke_cmd.step);

    const fmt = b.addFmt(.{
        .paths = &.{ "src", "tests", "build.zig" },
        .check = true,
    });

    const fmt_step = b.step("fmt", "Check Zig formatting");
    fmt_step.dependOn(&fmt.step);

    const lint_cmd = b.addSystemCommand(&.{
        b.graph.zig_exe,
        "build-exe",
        "-fno-emit-bin",
        "src/main.zig",
    });

    const lint_step = b.step("lint", "Check zcc compilation without emitting binary");
    lint_step.dependOn(&lint_cmd.step);

    const cli_module = b.createModule(.{
        .root_source_file = b.path("src/cli.zig"),
        .target = target,
        .optimize = optimize,
    });
    const cli_test_module = b.createModule(.{
        .root_source_file = b.path("src/cli_test.zig"),
        .target = target,
        .optimize = optimize,
    });
    cli_test_module.addImport("cli", cli_module);

    const cli_tests = b.addTest(.{ .root_module = cli_test_module });
    const run_cli_tests = b.addRunArtifact(cli_tests);

    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_cli_tests.step);
}

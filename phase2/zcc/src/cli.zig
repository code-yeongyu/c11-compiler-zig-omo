const std = @import("std");

pub const Mode = enum {
    compile,
    preprocess,
};

pub const Optimization = enum {
    O0,
    O2,
};

pub const ParseError = error{
    MissingOutputPath,
    EmptySanitizerList,
    UnknownOption,
};

pub const CliConfig = struct {
    help: bool = false,
    mode: Mode = .compile,
    output: ?[]const u8 = null,
    compile_only: bool = false,
    optimization: Optimization = .O0,
    debug_info: bool = false,
    sanitizers: []const []const u8 = &.{},
    inputs: []const []const u8 = &.{},

    pub fn deinit(self: *CliConfig, allocator: std.mem.Allocator) void {
        allocator.free(self.sanitizers);
        allocator.free(self.inputs);
        self.* = .{};
    }
};

pub const usage =
    \\Usage: zcc [options] file...
    \\
    \\Options:
    \\  --help              Print this help and exit
    \\  -E                  Run preprocessor-only token dump
    \\  -c                  Compile only, do not link
    \\  -o <path>           Write output to path
    \\  -O0 | -O2           Select optimization level
    \\  -g                  Emit debug information
    \\  -fsanitize=<list>   Enable sanitizer list
    \\
;

pub fn parse(allocator: std.mem.Allocator, args: []const []const u8) (std.mem.Allocator.Error || ParseError)!CliConfig {
    var config: CliConfig = .{};
    errdefer config.deinit(allocator);

    var sanitizers: std.ArrayList([]const u8) = .empty;
    defer sanitizers.deinit(allocator);

    var inputs: std.ArrayList([]const u8) = .empty;
    defer inputs.deinit(allocator);

    var i: usize = 0;
    while (i < args.len) : (i += 1) {
        const arg = args[i];
        if (std.mem.eql(u8, arg, "--help")) {
            config.help = true;
        } else if (std.mem.eql(u8, arg, "-E")) {
            config.mode = .preprocess;
        } else if (std.mem.eql(u8, arg, "-c")) {
            config.compile_only = true;
        } else if (std.mem.eql(u8, arg, "-o")) {
            i += 1;
            if (i == args.len) {
                return error.MissingOutputPath;
            }
            config.output = args[i];
        } else if (std.mem.eql(u8, arg, "-O0")) {
            config.optimization = .O0;
        } else if (std.mem.eql(u8, arg, "-O2")) {
            config.optimization = .O2;
        } else if (std.mem.eql(u8, arg, "-g")) {
            config.debug_info = true;
        } else if (std.mem.startsWith(u8, arg, "-fsanitize=")) {
            const value = arg["-fsanitize=".len..];
            if (value.len == 0) {
                return error.EmptySanitizerList;
            }
            try sanitizers.append(allocator, value);
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return error.UnknownOption;
        } else {
            try inputs.append(allocator, arg);
        }
    }

    config.sanitizers = try sanitizers.toOwnedSlice(allocator);
    config.inputs = try inputs.toOwnedSlice(allocator);
    return config;
}

pub fn errorMessage(err: ParseError) []const u8 {
    return switch (err) {
        error.MissingOutputPath => "expected path after -o",
        error.EmptySanitizerList => "expected sanitizer list after -fsanitize=",
        error.UnknownOption => "unknown option",
    };
}

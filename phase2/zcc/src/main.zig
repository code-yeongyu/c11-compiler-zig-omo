const std = @import("std");

const cli = @import("cli.zig");

pub fn main(init: std.process.Init) !u8 {
    const allocator = init.gpa;
    const argv = try init.minimal.args.toSlice(init.arena.allocator());

    var config = cli.parse(allocator, argv[1..]) catch |err| switch (err) {
        error.MissingOutputPath, error.EmptySanitizerList, error.UnknownOption => {
            var stderr_buffer: [1024]u8 = undefined;
            var stderr_file_writer: std.Io.File.Writer = .init(.stderr(), init.io, &stderr_buffer);
            const stderr = &stderr_file_writer.interface;
            defer stderr.flush() catch {};

            const message = switch (err) {
                error.MissingOutputPath => "expected path after -o",
                error.EmptySanitizerList => "expected sanitizer list after -fsanitize=",
                error.UnknownOption => "unknown option",
                else => unreachable,
            };
            try stderr.print("zcc: error: {s}\n\n{s}", .{ message, cli.usage });
            return 1;
        },
        else => |alloc_err| return alloc_err,
    };
    defer config.deinit(allocator);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout_file_writer: std.Io.File.Writer = .init(.stdout(), init.io, &stdout_buffer);
    const stdout = &stdout_file_writer.interface;
    defer stdout.flush() catch {};

    var stderr_buffer: [4096]u8 = undefined;
    var stderr_file_writer: std.Io.File.Writer = .init(.stderr(), init.io, &stderr_buffer);
    const stderr = &stderr_file_writer.interface;
    defer stderr.flush() catch {};

    if (config.help) {
        try stdout.writeAll(cli.usage);
        return 0;
    }

    if (config.inputs.len == 0) {
        try stderr.print("zcc: error: no input files\n\n{s}", .{cli.usage});
        return 1;
    }

    switch (config.mode) {
        .compile => {
            try stderr.writeAll("zcc: compile pipeline is not implemented yet\n");
            return 1;
        },
        .preprocess => try runPreprocessOnly(allocator, config.inputs, init.io, stdout),
    }

    return 0;
}

fn runPreprocessOnly(allocator: std.mem.Allocator, inputs: []const []const u8, io: std.Io, writer: *std.Io.Writer) !void {
    for (inputs) |path| {
        const source = try std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(16 * 1024 * 1024));
        defer allocator.free(source);

        try dumpTokens(path, source, writer);
    }
}

fn dumpTokens(path: []const u8, source: []const u8, writer: *std.Io.Writer) !void {
    var i: usize = 0;
    while (i < source.len) {
        const byte = source[i];
        if (std.ascii.isWhitespace(byte)) {
            i += 1;
            continue;
        }

        const start = i;
        if (isIdentStart(byte)) {
            i += 1;
            while (i < source.len and isIdentContinue(source[i])) : (i += 1) {}
            try writer.print("{s}: identifier {s}\n", .{ path, source[start..i] });
        } else if (std.ascii.isDigit(byte)) {
            i += 1;
            while (i < source.len and std.ascii.isDigit(source[i])) : (i += 1) {}
            try writer.print("{s}: number {s}\n", .{ path, source[start..i] });
        } else {
            i += 1;
            try writer.print("{s}: punctuator {s}\n", .{ path, source[start..i] });
        }
    }
}

fn isIdentStart(byte: u8) bool {
    return std.ascii.isAlphabetic(byte) or byte == '_';
}

fn isIdentContinue(byte: u8) bool {
    return isIdentStart(byte) or std.ascii.isDigit(byte);
}

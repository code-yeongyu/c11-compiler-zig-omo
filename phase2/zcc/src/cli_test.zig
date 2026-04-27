const std = @import("std");

const cli = @import("cli");

test "parse help exits before requiring inputs" {
    const allocator = std.testing.allocator;

    // given
    const args = &[_][]const u8{"--help"};

    // when
    var config = try cli.parse(allocator, args);
    defer config.deinit(allocator);

    // then
    try std.testing.expect(config.help);
    try std.testing.expectEqual(@as(usize, 0), config.inputs.len);
}

test "parse compile flags and inputs" {
    const allocator = std.testing.allocator;

    // given
    const args = &[_][]const u8{ "-c", "-O2", "-g", "-o", "hello.o", "hello.c" };

    // when
    var config = try cli.parse(allocator, args);
    defer config.deinit(allocator);

    // then
    try std.testing.expect(!config.help);
    try std.testing.expect(config.compile_only);
    try std.testing.expectEqual(cli.Optimization.O2, config.optimization);
    try std.testing.expect(config.debug_info);
    try std.testing.expectEqualStrings("hello.o", config.output.?);
    try std.testing.expectEqual(@as(usize, 1), config.inputs.len);
    try std.testing.expectEqualStrings("hello.c", config.inputs[0]);
}

test "parse preprocessor and sanitizer flags" {
    const allocator = std.testing.allocator;

    // given
    const args = &[_][]const u8{ "-E", "-fsanitize=undefined,address", "foo.c" };

    // when
    var config = try cli.parse(allocator, args);
    defer config.deinit(allocator);

    // then
    try std.testing.expectEqual(cli.Mode.preprocess, config.mode);
    try std.testing.expectEqual(@as(usize, 1), config.sanitizers.len);
    try std.testing.expectEqualStrings("undefined,address", config.sanitizers[0]);
    try std.testing.expectEqualStrings("foo.c", config.inputs[0]);
}

test "parse rejects missing output path" {
    const allocator = std.testing.allocator;

    // given
    const args = &[_][]const u8{"-o"};

    // when
    const result = cli.parse(allocator, args);

    // then
    try std.testing.expectError(error.MissingOutputPath, result);
}

test "parse rejects unknown options" {
    const allocator = std.testing.allocator;

    // given
    const args = &[_][]const u8{"-Wall"};

    // when
    const result = cli.parse(allocator, args);

    // then
    try std.testing.expectError(error.UnknownOption, result);
}

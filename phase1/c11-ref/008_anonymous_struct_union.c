struct anonymous_struct_union_box {
    int kind;
    union {
        struct {
            int x;
            int y;
        };
        long pair[2];
    };
};

struct anonymous_struct_union_result {
    int field_sum;
    long overlay_value;
};

struct anonymous_struct_union_result anonymous_struct_union_run(void)
{
    struct anonymous_struct_union_box box = {
        .kind = 3,
        .x = 10,
        .y = 20,
    };
    box.pair[1] = 44;
    return (struct anonymous_struct_union_result){
        .field_sum = box.kind + box.x,
        .overlay_value = box.pair[1],
    };
}

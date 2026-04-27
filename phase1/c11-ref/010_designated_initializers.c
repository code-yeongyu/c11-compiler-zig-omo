struct designated_initializers_entry {
    int id;
    int values[4];
};

struct designated_initializers_result {
    int first_sum;
    int sparse_value;
    int nested_value;
};

struct designated_initializers_result designated_initializers_run(void)
{
    struct designated_initializers_entry entries[3] = {
        [1] = { .id = 11, .values = { [0] = 2, [3] = 5 } },
        [0].id = 7,
        [0].values[2] = 13,
        [2] = { .id = 17, .values = { 19, 23, 29, 31 } },
    };

    return (struct designated_initializers_result){
        .first_sum = entries[0].id + entries[0].values[2],
        .sparse_value = entries[1].values[3],
        .nested_value = entries[2].values[1],
    };
}

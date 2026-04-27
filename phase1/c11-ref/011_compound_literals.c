struct compound_literals_point {
    int x;
    int y;
};

static int compound_literals_sum(const struct compound_literals_point *point)
{
    return point->x + point->y;
}

struct compound_literals_result {
    int scalar_sum;
    int array_sum;
    int pointer_sum;
};

struct compound_literals_result compound_literals_run(void)
{
    int *scalar = &(int){ 9 };
    int *array = (int[]){ 1, 3, 5, 7 };

    return (struct compound_literals_result){
        .scalar_sum = *scalar + 1,
        .array_sum = array[0] + array[3],
        .pointer_sum = compound_literals_sum(&(struct compound_literals_point){ .x = 12, .y = 30 }),
    };
}

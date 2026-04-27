int generic_classify_int(int value) { return value + 1; }
int generic_classify_char(char value) { return (int)value + 2; }
int generic_classify_double(double value) { return (int)value + 3; }
int generic_classify_pointer(const void *value) { return value == 0 ? -10 : 10; }

#define GENERIC_CLASSIFY(value) \
    _Generic((value), \
        char: generic_classify_char, \
        int: generic_classify_int, \
        double: generic_classify_double, \
        default: generic_classify_pointer \
    )(value)

struct generic_selection_result {
    int char_result;
    int int_result;
    int literal_result;
    int qualified_result;
    int pointer_result;
};

struct generic_selection_result generic_selection_run(void)
{
    char c = 5;
    const int qualified = 8;
    int storage = 1;

    return (struct generic_selection_result){
        .char_result = GENERIC_CLASSIFY(c),
        .int_result = GENERIC_CLASSIFY(7),
        .literal_result = GENERIC_CLASSIFY('a'),
        .qualified_result = GENERIC_CLASSIFY(qualified),
        .pointer_result = GENERIC_CLASSIFY(&storage),
    };
}

struct optional_feature_macros_result {
    int atomics;
    int threads;
    int complex_numbers;
    int vla;
};

struct optional_feature_macros_result optional_feature_macros_run(void)
{
    return (struct optional_feature_macros_result){
#if defined(__STDC_NO_ATOMICS__)
        .atomics = 0,
#else
        .atomics = 1,
#endif
#if defined(__STDC_NO_THREADS__)
        .threads = 0,
#else
        .threads = 1,
#endif
#if defined(__STDC_NO_COMPLEX__)
        .complex_numbers = 0,
#else
        .complex_numbers = 1,
#endif
#if defined(__STDC_NO_VLA__)
        .vla = 0,
#else
        .vla = 1,
#endif
    };
}

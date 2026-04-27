void invalid_vla_member(int n)
{
    struct invalid_vla_in_struct {
        int values[n];
    } value;
    (void)value;
}

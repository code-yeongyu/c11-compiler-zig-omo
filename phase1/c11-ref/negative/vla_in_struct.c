/* §6.7.2.1p9: a structure member shall not have variably modified type. */
void invalid_vla_member(int n)
{
    struct invalid_vla_in_struct {
        int values[n];
    } value;
    (void)value;
}

int main(void)
{
    return 0;
}

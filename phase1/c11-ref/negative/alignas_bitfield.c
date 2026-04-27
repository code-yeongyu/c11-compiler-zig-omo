/* §6.7.5p2: an alignment specifier shall not be applied to a bit-field. */
struct invalid_alignas_bitfield {
    _Alignas(8) int bits : 4;
};

int main(void)
{
    return 0;
}

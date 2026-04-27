struct invalid_alignas_bitfield {
    _Alignas(8) int bits : 4;
};

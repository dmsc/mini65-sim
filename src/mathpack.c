#include "mathpack.h"
#include "mathpack_bin.h"

int fp_init(sim65 s)
{
    sim65_add_data_rom(s, 0xD800, mathpack_bin, mathpack_bin_len);
    return 0;
}


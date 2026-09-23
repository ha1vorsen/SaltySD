#!/usr/bin/env python3

import sys

from arm_stack import bytes_reader, read_armips_symbols, verify_routine

BASE = 0x100000

# Start, end, require balanced stack
ROUTINES = (
    ("get_proj_new", "get_chr_new", True),
    ("get_chr_new", "cro_get_size_str", True),
    ("get_proj_size", "get_chr_size", True),
    ("get_chr_size", "get_weapon_class_name", True),
    ("get_fighter_data_redirect", "cro_get_fighter_data_str", True),
    ("cro_get_fighter_data_str", "chr_fighter_data_format", True),
    ("get_fighter_specializer_redirect", "cro_get_fighter_specializer_str", True),
    ("cro_get_fighter_specializer_str", "chr_fighter_specializer_format", True),
    ("get_weapon_data_redirect", "cro_get_weapon_data_str", True),
    ("cro_get_weapon_data_str", "chr_weapon_data_format", True),
    ("get_weapon_specializer_redirect", "cro_get_weapon_specializer_str", False),
    ("cro_get_weapon_specializer_str", "chr_weapon_specializer_format", True),
    ("cro_file_size_intercept", "cro_file_intercept", True),
    ("cro_file_intercept", "saltysd_cro_post", True),
    ("saltysd_cro_post_body", "cro_worker_fail_missing", True),
    ("cro_worker_fail_loaded", "cro_worker_fail_publish", True),
    ("cro_minigame_load_checked", "cro_minigame_load_checked_end", True),
)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: verify_cro_abi.py <code_saltysd.bin> <cro_redir.sym>")
    image = open(sys.argv[1], "rb").read()
    symbols = read_armips_symbols(sys.argv[2])
    read_word = bytes_reader(image, BASE)

    total_calls = 0
    for start_name, end_name, balanced in ROUTINES:
        missing = [name for name in (start_name, end_name) if name not in symbols]
        if missing:
            raise SystemExit(f"missing armips symbol(s): {', '.join(missing)}")
        try:
            calls, _exits = verify_routine(
                read_word, symbols[start_name], symbols[end_name], start_name,
                balanced=balanced,
            )
        except ValueError as error:
            raise SystemExit(str(error))
        total_calls += calls

    print(f"CRO ARM ABI ok: {len(ROUTINES)} routines, {total_calls} reachable calls")


if __name__ == "__main__":
    main()

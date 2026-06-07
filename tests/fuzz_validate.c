/**
 * @file fuzz_validate.c
 * @brief libFuzzer target for input validation functions.
 *
 * Feeds arbitrary byte sequences into every validate_*() function to discover
 * crashes, hangs, or assertion failures caused by unexpected inputs.
 *
 * Build (requires clang + compiler-rt):
 *   cmake -S . -B build-fuzz \
 *         -DCMAKE_C_COMPILER=clang \
 *         -DOPEN_LOTTO_FUZZ=ON \
 *         -DBUILD_TESTING=ON
 *   cmake --build build-fuzz --target fuzz_validate
 *
 * Run (recommended: start with a short campaign):
 *   ./build-fuzz/fuzz_validate -max_total_time=60
 *   ./build-fuzz/fuzz_validate -max_total_time=3600 -jobs=4
 *
 * Reproduce a crashing input:
 *   ./build-fuzz/fuzz_validate <crash-file>
 */

#include "../include/validate.h"
#include <stdint.h>
#include <string.h>

/* libFuzzer calls this for every generated input. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Limit input length to a safe working buffer; extra bytes are ignored. */
    char buf[513];
    size_t copy_len = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';

    /* --- validate_draw_count ------------------------------------------ */
    int draws = 0;
    validate_draw_count(buf, &draws);

    /* --- validate_export_format --------------------------------------- */
    validate_export_format(buf);

    /* --- validate_export_filename ------------------------------------- */
    validate_export_filename(buf);

    /* --- validate_log_level ------------------------------------------- */
    validate_log_level(buf);

    /* --- validate_gui_mode -------------------------------------------- */
    validate_gui_mode(buf);
    validate_gui_mode(NULL);

    /* --- validate_export_pair ----------------------------------------- */
    validate_export_pair(buf, buf);
    validate_export_pair(buf, NULL);
    validate_export_pair(NULL, buf);
    validate_export_pair(NULL, NULL);

    /* --- validate_option_conflicts ------------------------------------ */
    /* Derive simple integer flags from the first byte to vary call paths. */
    int first = size > 0 ? (int)(data[0] & 0x03) : 0;
    validate_option_conflicts(first & 1, (first >> 1) & 1, size > 0 ? buf : NULL);

    return 0; /* Non-zero return is reserved for libFuzzer "interesting" hint */
}

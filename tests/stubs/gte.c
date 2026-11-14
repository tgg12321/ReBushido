/* A stand-in for the GTE, the PlayStation's geometry coprocessor, as far as
 * the tests need one. tools/modern/check.py's headers turn each of Sony's
 * GTE macros into a call of modern_gte("name", arguments...); this is that
 * function, for the one operation the tested code uses: counting a
 * number's leading zeros (gte_Lzc, which is gte_ldlzc, two gte_nop and
 * gte_stlzc). A test that reaches any other operation fails, saying which.
 * Included by a test after runtime/runtime.h. */

/* The GTE's LZCS register: the number whose leading bits are counted. */
static s32 g_gte_lzcs;

/* What the GTE's LZCR register reads: how many of the number's top bits
 * are the same as its sign, 1 to 32. */
static s32 gte_LeadingBits(s32 value) {
    s32 count = 0;
    u32 bits = (value < 0) ? ~(u32)value : (u32)value;

    while (count < 32 && !(bits & 0x80000000U)) {
        bits <<= 1;
        count++;
    }
    return count;
}

void modern_gte(const char *op, ...) {
    __builtin_va_list args;

    __builtin_va_start(args, op);
    if (test_StringsEqual(op, "gte_ldlzc")) {
        g_gte_lzcs = __builtin_va_arg(args, s32);
    } else if (test_StringsEqual(op, "gte_stlzc")) {
        *__builtin_va_arg(args, s32 *) = gte_LeadingBits(g_gte_lzcs);
    } else if (!test_StringsEqual(op, "gte_nop")) {
        test_Count(0, __FILE__, __LINE__,
                   "the tested code used %s, which tests/stubs/gte.c has not",
                   op);
    }
    __builtin_va_end(args);
}

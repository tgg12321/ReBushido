"""math_HsvToRgb and math_RgbToHsv against colorsys. All six numbers are
shares of ONE (0x1000)."""
import colorsys

ONE = 0x1000
# The conversions cut every product and quotient to twelve bits.
RGB_TOLERANCE = 3
HSV_TOLERANCE = 6


def check(lines, checks):
    for line in lines:
        h, s, v, r, g, b, back_h, back_s, back_v = [int(word) for word in line.split()]
        what = 'hsv (%d, %d, %d)' % (h, s, v)
        want = colorsys.hsv_to_rgb(h / ONE, s / ONE, v / ONE)
        for name, got, exact in zip('rgb', (r, g, b), want):
            checks.near('%s: %s' % (what, name), got, ONE * exact, RGB_TOLERANCE)

        # and back again. A gray has no hue and black no saturation: both
        # read back as 0. A hue of a whole turn is the hue 0.
        checks.near('%s: value read back' % what, back_v, v, RGB_TOLERANCE)
        if v == 0:
            checks.equal('%s: saturation read back' % what, back_s, 0)
            continue
        checks.near('%s: saturation read back' % what, back_s, s, HSV_TOLERANCE)
        if s == 0:
            checks.equal('%s: hue read back' % what, back_h, 0)
            continue
        # the darker and the grayer a colour, the fewer bits its hue has
        hue_tolerance = HSV_TOLERANCE * (ONE / v) * (ONE / s)
        apart = abs(back_h - h % ONE)
        checks.true('%s: hue read back is %d' % (what, back_h),
                    min(apart, ONE - apart) <= hue_tolerance)

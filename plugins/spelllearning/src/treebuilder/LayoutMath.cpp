#include "treebuilder/LayoutMath.h"

#include <bit>
#include <cmath>
#include <cstdint>

// =============================================================================
// fdlibm sin, cos and atan2 (as V8's base/ieee754.cc carries them)
// =============================================================================
//
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// Every constant is written as its bits, so no decimal can be misread. The
// argument reduction covers |x| up to 2^19 x pi/2 (the medium case); larger
// arguments - a spell index times the golden angle would need hundreds of
// thousands of spells - go to the C runtime.

namespace
{
    // =========================================================================
    // WORDS
    // =========================================================================

    std::int32_t HighWord(double x)
    {
        return static_cast<std::int32_t>(std::bit_cast<std::uint64_t>(x) >> 32);
    }

    std::uint32_t LowWord(double x)
    {
        return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x) & 0xFFFFFFFFu);
    }

    double FromWords(std::uint32_t hi, std::uint32_t lo)
    {
        return std::bit_cast<double>((static_cast<std::uint64_t>(hi) << 32) | lo);
    }

    double Bits(std::uint64_t bits) { return std::bit_cast<double>(bits); }

    // =========================================================================
    // CONSTANTS
    // =========================================================================

    const double kOne = 1.0;
    const double kHalf = 0.5;
    const double kHuge = 1.0e300;
    const double kTiny = 1.0e-300;

    // atan
    const double kAtanHi[] = {
        Bits(0x3FDDAC670561BB4Full),  // atan(0.5) hi
        Bits(0x3FE921FB54442D18ull),  // atan(1.0) hi
        Bits(0x3FEF730BD281F69Bull),  // atan(1.5) hi
        Bits(0x3FF921FB54442D18ull),  // atan(inf) hi
    };
    const double kAtanLo[] = {
        Bits(0x3C7A2B7F222F65E2ull),
        Bits(0x3C81A62633145C07ull),
        Bits(0x3C7007887AF0CBBDull),
        Bits(0x3C91A62633145C07ull),
    };
    const double kAT[] = {
        Bits(0x3FD555555555550Dull),
        Bits(0xBFC999999998EBC4ull),
        Bits(0x3FC24924920083FFull),
        Bits(0xBFBC71C6FE231671ull),
        Bits(0x3FB745CDC54C206Eull),
        Bits(0xBFB3B0F2AF749A6Dull),
        Bits(0x3FB10D66A0D03D51ull),
        Bits(0xBFADDE2D52DEFD9Aull),
        Bits(0x3FA97B4B24760DEBull),
        Bits(0xBFA2B4442C6A6C2Full),
        Bits(0x3F90AD3AE322DA11ull),
    };

    // atan2
    const double kPiO4 = Bits(0x3FE921FB54442D18ull);
    const double kPiO2 = Bits(0x3FF921FB54442D18ull);
    const double kPi = Bits(0x400921FB54442D18ull);
    const double kPiLo = Bits(0x3CA1A62633145C07ull);

    // __kernel_sin
    const double kS1 = Bits(0xBFC5555555555549ull);
    const double kS2 = Bits(0x3F8111111110F8A6ull);
    const double kS3 = Bits(0xBF2A01A019C161D5ull);
    const double kS4 = Bits(0x3EC71DE357B1FE7Dull);
    const double kS5 = Bits(0xBE5AE5E68A2B9CEBull);
    const double kS6 = Bits(0x3DE5D93A5ACFD57Cull);

    // __kernel_cos
    const double kC1 = Bits(0x3FA555555555554Cull);
    const double kC2 = Bits(0xBF56C16C16C15177ull);
    const double kC3 = Bits(0x3EFA01A019CB1590ull);
    const double kC4 = Bits(0xBE927E4F809C52ADull);
    const double kC5 = Bits(0x3E21EE9EBDB4B1C4ull);
    const double kC6 = Bits(0xBDA8FAE9BE8838D4ull);

    // __ieee754_rem_pio2
    const double kInvPio2 = Bits(0x3FE45F306DC9C883ull);
    const double kPio2_1 = Bits(0x3FF921FB54400000ull);
    const double kPio2_1t = Bits(0x3DD0B4611A626331ull);
    const double kPio2_2 = Bits(0x3DD0B4611A600000ull);
    const double kPio2_2t = Bits(0x3BA3198A2E037073ull);
    const double kPio2_3 = Bits(0x3BA3198A2E000000ull);
    const double kPio2_3t = Bits(0x397B839A252049C1ull);

    // High words of n x pi/2, n = 1..32
    const std::int32_t kNPio2Hw[] = {
        0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
        0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
        0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
        0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
        0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
        0x404858EB, 0x404921FB,
    };

    constexpr std::int32_t kMediumLimit = 0x413921FB;  // 2^19 x pi/2
    constexpr int kLarge = 0x7FFFFFFF;                  // rem_pio2: too large, not reduced

    // =========================================================================
    // ATAN
    // =========================================================================

    double Atan(double x)
    {
        const std::int32_t hx = HighWord(x);
        const std::int32_t ix = hx & 0x7FFFFFFF;
        int id;
        if (ix >= 0x44100000) {  // |x| >= 2^66
            const std::uint32_t low = LowWord(x);
            if (ix > 0x7FF00000 || (ix == 0x7FF00000 && low != 0)) return x + x;  // NaN
            if (hx > 0) return kAtanHi[3] + kAtanLo[3];
            return -kAtanHi[3] - kAtanLo[3];
        }
        if (ix < 0x3FDC0000) {       // |x| < 0.4375
            if (ix < 0x3E400000) {   // |x| < 2^-27
                if (kHuge + x > kOne) return x;
            }
            id = -1;
        } else {
            x = std::fabs(x);
            if (ix < 0x3FF30000) {       // |x| < 1.1875
                if (ix < 0x3FE60000) {   // 7/16 <= |x| < 11/16
                    id = 0;
                    x = (2.0 * x - kOne) / (2.0 + x);
                } else {                 // 11/16 <= |x| < 19/16
                    id = 1;
                    x = (x - kOne) / (x + kOne);
                }
            } else {
                if (ix < 0x40038000) {   // |x| < 2.4375
                    id = 2;
                    x = (x - 1.5) / (kOne + 1.5 * x);
                } else {                 // 2.4375 <= |x| < 2^66
                    id = 3;
                    x = -1.0 / x;
                }
            }
        }
        const double z = x * x;
        const double w = z * z;
        const double s1 = z * (kAT[0] + w * (kAT[2] + w * (kAT[4] + w * (kAT[6] + w * (kAT[8] + w * kAT[10])))));
        const double s2 = w * (kAT[1] + w * (kAT[3] + w * (kAT[5] + w * (kAT[7] + w * kAT[9]))));
        if (id < 0) return x - x * (s1 + s2);
        const double r = kAtanHi[id] - ((x * (s1 + s2) - kAtanLo[id]) - x);
        return (hx < 0) ? -r : r;
    }

    // =========================================================================
    // SIN / COS KERNELS (|x| <= pi/4)
    // =========================================================================

    double KernelSin(double x, double y, int iy)
    {
        const std::int32_t ix = HighWord(x) & 0x7FFFFFFF;
        if (ix < 0x3E400000) {  // |x| < 2^-27
            if (static_cast<int>(x) == 0) return x;
        }
        const double z = x * x;
        const double v = z * x;
        const double r = kS2 + z * (kS3 + z * (kS4 + z * (kS5 + z * kS6)));
        if (iy == 0) return x + v * (kS1 + z * r);
        return x - ((z * (kHalf * y - v * r) - y) - v * kS1);
    }

    double KernelCos(double x, double y)
    {
        const std::int32_t ix = HighWord(x) & 0x7FFFFFFF;
        if (ix < 0x3E400000) {  // |x| < 2^-27
            if (static_cast<int>(x) == 0) return kOne;
        }
        const double z = x * x;
        const double r = z * (kC1 + z * (kC2 + z * (kC3 + z * (kC4 + z * (kC5 + z * kC6)))));
        if (ix < 0x3FD33333) {  // |x| < 0.3
            return kOne - (0.5 * z - (z * r - x * y));
        }
        double qx;
        if (ix > 0x3FE90000) {  // |x| > 0.78125
            qx = 0.28125;
        } else {
            qx = FromWords(static_cast<std::uint32_t>(ix - 0x00200000), 0);  // x/4
        }
        const double iz = 0.5 * z - qx;
        const double a = kOne - qx;
        return a - (iz - (z * r - x * y));
    }

    // =========================================================================
    // ARGUMENT REDUCTION
    // =========================================================================

    // x - n x pi/2 into y[0] + y[1]; returns n, or kLarge past the medium case.
    int RemPio2(double x, double* y)
    {
        const std::int32_t hx = HighWord(x);
        const std::int32_t ix = hx & 0x7FFFFFFF;
        if (ix <= 0x3FE921FB) {  // |x| <= pi/4
            y[0] = x;
            y[1] = 0;
            return 0;
        }
        if (ix < 0x4002D97C) {  // |x| < 3pi/4
            if (hx > 0) {
                double z = x - kPio2_1;
                if (ix != 0x3FF921FB) {
                    y[0] = z - kPio2_1t;
                    y[1] = (z - y[0]) - kPio2_1t;
                } else {
                    z -= kPio2_2;
                    y[0] = z - kPio2_2t;
                    y[1] = (z - y[0]) - kPio2_2t;
                }
                return 1;
            }
            double z = x + kPio2_1;
            if (ix != 0x3FF921FB) {
                y[0] = z + kPio2_1t;
                y[1] = (z - y[0]) + kPio2_1t;
            } else {
                z += kPio2_2;
                y[0] = z + kPio2_2t;
                y[1] = (z - y[0]) + kPio2_2t;
            }
            return -1;
        }
        if (ix > kMediumLimit) return kLarge;

        double t = std::fabs(x);
        const std::int32_t n = static_cast<std::int32_t>(t * kInvPio2 + kHalf);
        const double fn = static_cast<double>(n);
        double r = t - fn * kPio2_1;
        double w = fn * kPio2_1t;
        if (n < 32 && ix != kNPio2Hw[n - 1]) {
            y[0] = r - w;
        } else {
            const std::int32_t j = ix >> 20;
            y[0] = r - w;
            std::int32_t i = j - ((HighWord(y[0]) >> 20) & 0x7FF);
            if (i > 16) {  // second iteration
                t = r;
                w = fn * kPio2_2;
                r = t - w;
                w = fn * kPio2_2t - ((t - r) - w);
                y[0] = r - w;
                i = j - ((HighWord(y[0]) >> 20) & 0x7FF);
                if (i > 49) {  // third iteration
                    t = r;
                    w = fn * kPio2_3;
                    r = t - w;
                    w = fn * kPio2_3t - ((t - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) {
            y[0] = -y[0];
            y[1] = -y[1];
            return -n;
        }
        return n;
    }
}

namespace LayoutMath
{
    // =========================================================================
    // PUBLIC
    // =========================================================================

    double Sin(double x)
    {
        const std::int32_t ix = HighWord(x) & 0x7FFFFFFF;
        if (ix <= 0x3FE921FB) return KernelSin(x, 0.0, 0);
        if (ix >= 0x7FF00000) return x - x;
        double y[2];
        const int n = RemPio2(x, y);
        if (n == kLarge) return std::sin(x);
        switch (n & 3) {
        case 0: return KernelSin(y[0], y[1], 1);
        case 1: return KernelCos(y[0], y[1]);
        case 2: return -KernelSin(y[0], y[1], 1);
        default: return -KernelCos(y[0], y[1]);
        }
    }

    double Cos(double x)
    {
        const std::int32_t ix = HighWord(x) & 0x7FFFFFFF;
        if (ix <= 0x3FE921FB) return KernelCos(x, 0.0);
        if (ix >= 0x7FF00000) return x - x;
        double y[2];
        const int n = RemPio2(x, y);
        if (n == kLarge) return std::cos(x);
        switch (n & 3) {
        case 0: return KernelCos(y[0], y[1]);
        case 1: return -KernelSin(y[0], y[1], 1);
        case 2: return -KernelCos(y[0], y[1]);
        default: return KernelSin(y[0], y[1], 1);
        }
    }

    double Atan2(double y, double x)
    {
        const std::int32_t hx = HighWord(x);
        const std::uint32_t lx = LowWord(x);
        const std::int32_t ix = hx & 0x7FFFFFFF;
        const std::int32_t hy = HighWord(y);
        const std::uint32_t ly = LowWord(y);
        const std::int32_t iy = hy & 0x7FFFFFFF;
        if (std::isnan(x) || std::isnan(y)) return x + y;
        if (((hx - 0x3FF00000) | static_cast<std::int32_t>(lx)) == 0) return Atan(y);  // x = 1.0
        int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);  // 2 x sign(x) + sign(y)

        if ((iy | static_cast<std::int32_t>(ly)) == 0) {  // y = 0
            switch (m) {
            case 0:
            case 1: return y;
            case 2: return kPi + kTiny;
            default: return -kPi - kTiny;
            }
        }
        if ((ix | static_cast<std::int32_t>(lx)) == 0) return (hy < 0) ? -kPiO2 - kTiny : kPiO2 + kTiny;  // x = 0

        if (ix == 0x7FF00000) {  // x infinite
            if (iy == 0x7FF00000) {
                switch (m) {
                case 0: return kPiO4 + kTiny;
                case 1: return -kPiO4 - kTiny;
                case 2: return 3.0 * kPiO4 + kTiny;
                default: return -3.0 * kPiO4 - kTiny;
                }
            }
            switch (m) {
            case 0: return 0.0;
            case 1: return -0.0;
            case 2: return kPi + kTiny;
            default: return -kPi - kTiny;
            }
        }
        if (iy == 0x7FF00000) return (hy < 0) ? -kPiO2 - kTiny : kPiO2 + kTiny;  // y infinite

        double z;
        const std::int32_t k = (iy - ix) >> 20;
        if (k > 60) {  // |y/x| > 2^60
            z = kPiO2 + 0.5 * kPiLo;
            m &= 1;
        } else if (hx < 0 && k < -60) {
            z = 0.0;
        } else {
            z = Atan(std::fabs(y / x));
        }
        switch (m) {
        case 0: return z;
        case 1: return -z;
        case 2: return kPi - (z - kPiLo);
        default: return (z - kPiLo) - kPi;
        }
    }
}

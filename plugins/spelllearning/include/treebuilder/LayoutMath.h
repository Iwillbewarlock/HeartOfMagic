#pragma once

// =============================================================================
// LayoutMath - sin, cos and atan2 exactly as the panel's JavaScript works them
// =============================================================================
//
// The tree declutter pass (LayoutDeclutter) exists twice: in the panel's
// JavaScript (tests, and the fallback) and here. Both must move every spell to
// the same place, and the search compares costs built from thousands of angles:
// one last-bit difference in an angle can pick another spot, and everything
// after it follows. V8's Math.sin, Math.cos and Math.atan2 (node, which runs
// the panel's tests and the reference trees) are fdlibm; the MSVC runtime's
// differ from it in the last bit for about one atan2 in five. These are the
// fdlibm routines, so the results match the JavaScript bit for bit, and do not
// change with the compiler or its runtime. sqrt, floor and fmod need nothing:
// IEEE 754 rounds them exactly, the same everywhere.

namespace LayoutMath
{
    double Sin(double x);
    double Cos(double x);
    double Atan2(double y, double x);
}

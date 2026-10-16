# Skald design pictures

Sources of `PrismaUI/views/SpellLearning/SpellLearningPanel/themes/skald/corner.png` (200x200) and
`empty.png` (480x320). `common.js` is the explainer pictures' palette and glyphs, `knots.js` the woven
knotwork (trefoil, two-strand plait, over-under found per crossing).

Render with headless Chrome, transparent background:

```bash
chrome --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 --default-background-color=00000000 --window-size=200,200 --screenshot=corner.png file:///<path>/corner.html
chrome --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 --default-background-color=00000000 --window-size=480,320 --screenshot=empty.png file:///<path>/empty.html
```

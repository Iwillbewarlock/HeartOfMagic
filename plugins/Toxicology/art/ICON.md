# Mimic poison icon (I4)

The addon puts the keyword `HoMTox_MimicPoison` on the poison it is mimicking (spec 5.5). Inventory Interface
Information Injector (I4) then draws that poison with this icon:

| File | Role |
|---|---|
| `data/SKSE/Plugins/InventoryInjector/HeartOfMagic_Toxicology.json` | I4 rule: `formType` `Potion` + keyword `HoMTox_MimicPoison` -> `iconSource` `HeartOfMagicToxicology/icons.swf`, `iconLabel` `potion_mimic`, `iconColor` `#62A636` |
| `data/Interface/HeartOfMagicToxicology/icons.swf` | The icon: CWS v10, 128 x 128 stage, one icon frame labelled `potion_mimic` |
| `art/icon-concept-gpt.png` | The chosen concept (GPT image generation) |
| `art/icon-concept-gemini.jpg` | The other concept (Gemini), kept for the record |
| `art/icon.svg` | The vector drawn by hand from the GPT concept; the SWF's shape is imported from it |

The `$schema` URL in the json (`Exit-9B/InventoryInjector/main/docs/InventoryInjector.schema.json`) answered
HTTP 200 on 2026-10-17, and the rule's fields are in that schema (`keywords` takes a string or an array).

## The drawing

`icon.svg` is written in the concept image's own pixel grid (1254 px) and scaled into 94 px by one group
transform, `matrix(0.1 0 0 0.1 -15.7 -15.15)`. FFDec 18.5 ignores `viewBox` on import (the first import came in
at 1000 px), so the scale has to be a transform. White fill, no stroke; I4's `iconColor` recolours it.

- Rings centred at (627, 600): outer 394-440, inner 260-304. Both stop at a gap circle round the flask (centre
  (627, 842), radius 273 = flask body 241 + 32 gap). The end points are the two circles' intersections
  (`a = (r^2 - g^2 + d^2) / 2d`, `x = sqrt(r^2 - a^2)`, d = 242), worked out with `node -e`.
- Three diamonds (half diagonal 34) and two dots (radius 28) on the circle of radius 349 between the rings.
- Flask: body circle (627, 842) r 241, neck 545-709 with a curved shoulder, collar pill 519-735 x 474-540, tapered
  cork 541-713 at the top, 565-689 at y 466. Thin gaps (8-10 units) keep cork, collar and neck apart, as in the
  concept.
- Arcs are SVG `A` commands; FFDec's importer turns them into quadratic curves.

Seen at 24 px (FFDec `-zoom 0.1875`) the flask is solid and the two rings still read as rings.

## Building the SWF

Tools: FFDec 18.5.0 (`D:\TAKEALOOK\mods\Dynamic Interface Patcher - DIP\DIP\ffdec\ffdec.jar`), run with
`"C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot\bin\java.exe" -jar ffdec.jar` (the bundled `ffdec.bat`
points at a Java that is not there). Template: the I4 icon `Disease.swf` from "Diseases - I4 icon"
(`Interface\IAmNotSimon\Disease.swf`), copied to a scratch folder; nothing under the template's mod is changed.

Template layout (`-dumpSWF`): frame 1 is `DoAction` (stop) + `DefineShape2` (1) placed at depth 3 with alpha 0
(placeholder); frame 2 is `FrameLabel` + `DefineShape` (2, the white icon) + `DefineSprite` (3) wrapping shape 2 +
`PlaceObject2` of the sprite at depth 4.

```
java -jar ffdec.jar -importShapes template.swf step1.swf import      # import/shapes/2.svg = art/icon.svg
java -jar ffdec.jar -swf2xml step1.swf step1.xml
#   in step1.xml: FrameLabelTag name="effect_disease" -> "potion_mimic"
#   and the sprite's PlaceObject2 matrix scaleY 88681 -> 87120 (= scaleX), translateY 10 -> 30
sed -e 's|<item type="FrameLabelTag" name="effect_disease"|<item type="FrameLabelTag" name="potion_mimic"|' \
    -e 's|scaleX="87120" scaleY="88681" translateX="30" translateY="10"|scaleX="87120" scaleY="87120" translateX="30" translateY="30"|' \
    step1.xml > step2.xml
java -jar ffdec.jar -xml2swf step2.xml icons.swf
```

The template stretches its icon 1.8 % taller than wide; the matrix edit makes the scale even so the rings and the
flask stay round, and centres the icon on the 128 px stage. No custom tool was needed: FFDec's XML round trip
writes the label and the matrix and keeps the SWF zlib-compressed (header `CWS`, version 10).

Shape 2 keeps the template's bounds (0.05-93.7 px); the new drawing lies inside them (3-91 x 0.85-93.15 px).

## Checking it

```
java -jar ffdec.jar -dumpSWF icons.swf                 # FrameLabel (potion_mimic), DefineShape (2) fill ff ff ff,
                                                       # DefineSprite (3) with PlaceObject2 (2), PlaceObject2 (3)
java -jar ffdec.jar -zoom 4 -export frame out icons.swf       # 2.png: the white icon on black; 1.png empty
java -jar ffdec.jar -zoom 0.1875 -export frame out24 icons.swf # the icon at 24 px
```

To change the drawing: edit `icon.svg`, then run the three steps above again from the template.

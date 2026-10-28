Heart of Magic - fonts for the "book" designs (Arcane, Night Grimoire)
=====================================================================

All fonts come from Google Fonts' official repository (https://github.com/google/fonts, branch
main, folder ofl/<family>/) and are licensed under the SIL Open Font License 1.1. Each family's
licence is kept here as <Family>-OFL.txt, copied unchanged from the same repository folder.

Reserved Font Names: none of the eight OFL.txt files (nor the fonts' own name tables) declares a
Reserved Font Name, so the internal family names are kept (no renaming needed). Fonts that do carry
an RFN were rejected for that reason: Lora ("Lora"), Libre Baskerville, IM Fell English
("IM FELL English Roman" in its name table), Nanum Myeongjo. The Noto fonts' name table says
"(c) Adobe" while Google Fonts' OFL.txt header says "Google Inc."; the licence is the same OFL 1.1.

How the files were made (fontTools 4.66):
  1. Variable fonts were pinned to one static instance (fontTools.varLib.instancer); no file here
     is variable. Name table and OS/2 weight/style bits were set to match the instance.
  2. Subset with python -m fontTools.subset: hinting removed (--no-hinting), DSIG/STAT/vhea/vmtx/
     meta dropped, all remaining name records kept. Latin/Cyrillic files keep every OpenType layout
     feature; CJK files keep ccmp, locl, kern, liga, calt, mark, mkmk, palt.
  3. Every file keeps whatever the source font has of: Basic Latin, Latin-1, Latin Extended-A,
     General Punctuation (U+2000-206F), currency signs (U+20A0-20CF: won, euro, rouble...),
     numero, trademark, arrows (U+2190-21FF), a few math signs, basic geometric shapes and stars,
     check/cross marks (U+2713-2717), and every character used by the matching lang/*.json file(s).
     The CJK files also keep CJK Symbols and Punctuation (U+3000-303F) and half/full-width forms
     (U+FF00-FFEF).

Suggested stacks: put the Latin body first for every language, so Latin letters, digits and
punctuation (— – · …) inside CJK strings come from Literata; the CJK face supplies the rest.
  Body (both designs): "HoM Book Body", "HoM Book Body KO" / "... JA" / "... SC" / "... TC", serif
  Arcane titles:       "HoM Book Title Arcane", "HoM Book Body", <CJK body>, serif
  Night Grimoire titles: "HoM Book Title NG", "HoM Book Title NG Cyrillic", "HoM Book Title Arcane",
                       "HoM Book Body", <CJK body>, serif
(Cinzel has no Cyrillic: Russian titles fall through to the Alegreya SC Cyrillic file, which is
also caps/small caps. CJK titles use the CJK body faces.)


book-body-latin.ttf        Literata Regular (family "Literata", weight 400)       181 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/literata/Literata[opsz,wght].ttf
  Licence: Literata-OFL.txt
  Instance: opsz 10, wght 400. The small optical size gives looser spacing and sturdier strokes,
           chosen because the UI sets most text at 11-14 px (checked on indigo and on parchment).
  Kept:    Latin, Latin-1, Latin Extended-A/B, Latin Extended Additional, combining marks,
           Cyrillic (U+0400-052F), punctuation and symbols, all layout features.
           Covers en, de, es, fr, it, pl, pt-br, tr and ru.

book-body-latin-bold.ttf   Literata Bold (family "Literata", weight 700)          186 KB
  Source / licence: as above. Instance opsz 10, wght 700. Kept: same as the regular file.

book-title-arcane-latin.ttf  EB Garamond SemiBold (family "EB Garamond SemiBold", typographic
                           family "EB Garamond", weight 600)                      320 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/ebgaramond/EBGaramond[wght].ttf
  Licence: EBGaramond-OFL.txt
  Instance: wght 600.
  Kept:    same ranges as the Latin body, Cyrillic included (covers all nine Latin/Cyrillic langs).

book-title-ng-latin.ttf    Cinzel SemiBold (family "Cinzel SemiBold", typographic family
                           "Cinzel", weight 600)                                   69 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/cinzel/Cinzel[wght].ttf
  Licence: Cinzel-OFL.txt
  Instance: wght 600. Inscriptional capitals; lowercase letters are small capitals.
  Kept:    Latin, Latin-1, Latin Extended-A/B/Additional, punctuation. Covers en, de, es, fr, it,
           pl, pt-br, tr.
  Gaps:    the font has no Cyrillic, no won or rouble sign, no arrows, no numero sign.

book-title-ng-cyrillic.ttf Alegreya SC Medium (family "Alegreya SC Medium", typographic family
                           "Alegreya SC", weight 500)                              39 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/alegreyasc/AlegreyaSC-Medium.ttf
  Licence: AlegreyaSC-OFL.txt
  Kept:    space, no-break space, Cyrillic (U+0400-052F), numero sign. Only a fallback behind
           Cinzel for Russian titles.

book-body-ko.ttf           Hahmlet Regular (family "Hahmlet", weight 400)       1,375 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/hahmlet/Hahmlet[wght].ttf
  Licence: Hahmlet-OFL.txt
  Instance: wght 400.
  Kept:    all 2,788 Hangul syllables the font has (the 2,350 of KS X 1001, all present, plus
           438 more), Hangul Jamo / compatibility Jamo it has, basic Latin, punctuation,
           CJK punctuation, every character of ko.json.
  Gaps:    syllables outside those 2,788 (the font does not have them).

book-body-ja.ttf           BIZ UDPMincho Regular (family "BIZ UDPMincho", weight 400)  910 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/bizudpmincho/BIZUDPMincho-Regular.ttf
  Licence: BIZUDPMincho-OFL.txt
  Kept:    Hiragana, Katakana (+ phonetic extensions), CJK punctuation, half/full-width forms,
           the Joyo kanji (2,140 code points marked kJoyoKanji in Unicode's Unihan database: the
           2,136 Joyo characters, including U+20B9F, plus the 4 variant code points Unihan lists for
           them; all present), every character of ja.json.
  Gaps:    no won or rouble sign; its em dash is faint at 11 px (Literata, first in the stack,
           supplies it).

book-body-zh-cn.ttf        Noto Serif SC Medium (family "Noto Serif SC Medium", typographic
                           family "Noto Serif SC", weight 500)                  1,594 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/notoserifsc/NotoSerifSC[wght].ttf
  Licence: NotoSerifSC-OFL.txt
  Instance: wght 500 (the 400 hairlines fade on the dark Night Grimoire pages; 500 still reads
           well on parchment).
  Kept:    GB2312 level-1 hanzi (3,755, bytes 0xB0A1-0xD7F9; all present), CJK punctuation,
           full-width forms, every character of zh-cn.json.
  Gaps:    no rouble sign and none of the pl/tr Latin Extended-A letters (neither is needed by
           zh-cn.json; Literata, first in the stack, has them).

book-body-zh-tw.ttf        Noto Serif TC Medium (family "Noto Serif TC Medium", typographic
                           family "Noto Serif TC", weight 500)                  2,625 KB
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/notoseriftc/NotoSerifTC[wght].ttf
  Licence: NotoSerifTC-OFL.txt
  Instance: wght 500 (same reason as zh-cn).
  Kept:    Big5 level-1 hanzi (5,401, lead bytes 0xA4-0xC6; all present, nothing trimmed - the
           file stays under 3 MB), CJK punctuation, full-width forms, every character of zh-tw.json.
  Gaps:    as zh-cn (no rouble sign, no pl/tr letters).

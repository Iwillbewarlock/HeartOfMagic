Heart of Magic - "Chalkboard" design handwriting fonts
=======================================================

All fonts are open fonts under the SIL Open Font License 1.1. Each font's licence sits next to it
(<Family>-OFL.txt, copied unchanged from the source repository; HakgyoansimBunpil-LICENSE.txt, see
below). Every .ttf here is a subset (a "Modified Version" under the OFL) made with fontTools
(python -m fontTools.subset): hinting removed, DSIG/BASE/meta/vhea/vmtx dropped, all name
records kept. For the CJK and Korean fonts, only the layout features ccmp, locl, kern, liga,
calt, mark, mkmk and palt are kept; Pangolin keeps all features.

Reserved Font Names: none of the licences declares a Reserved Font Name, so every family keeps
its original internal name (no renaming was needed).

Every file keeps whatever the source font has of: Basic Latin, Latin-1, Latin Extended-A,
General Punctuation (U+2000-206F), currency signs (U+20A0-20CF), numero, trademark, arrows
(U+2190-21FF), a few math signs, basic geometric shapes and stars, check/cross marks
(U+2713-2717), and every character used by the matching lang/*.json file.

hom-chalk-ko.ttf        Gaegu Regular - Korean body text      (family "Gaegu")
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/gaegu/Gaegu-Regular.ttf
  Licence: Gaegu-OFL.txt
  Kept:    all 2,350 Hangul syllables the font has (identical to the KS X 1001 set), the won
           sign, basic Latin, CJK punctuation it has.
  Gaps:    the font has no Latin Extended-A and no · (U+00B7), – — (U+2013/2014), × … • € ™ or
           arrows; ko.json uses · and —. Put a fallback font after this one in font-family.

hom-chalk-ko-bold.ttf   Gaegu Bold                             (family "Gaegu", Bold)
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/gaegu/Gaegu-Bold.ttf
  Licence: Gaegu-OFL.txt
  Kept / gaps: same as hom-chalk-ko.ttf.

hom-chalk-ko-title.ttf  Hakgyoansim Bunpil R (학교안심 분필) - Korean headings
                                                     (family "Hakgyoansim Bunpil R" / "학교안심 분필 R")
  Source:  the official KERIS Education Copyright Support Center download
           https://copyright.keris.or.kr/wft/fntDwnldView?fntGrpId=GFT202301060000000000005
           (zip with HakgyoansimBunpilR.ttf/.otf; the .ttf was used). Copyright KERIS, design by
           Yoon Design.
  Licence: HakgyoansimBunpil-LICENSE.txt - the package has no licence file; this file records the
           KERIS page's statement (OFL 1.1; modification and redistribution allowed; no Reserved
           Font Name) followed by the standard OFL 1.1 text.
  Kept:    the 2,350 KS X 1001 Hangul syllables (the font has all 11,172), Hangul Jamo and
           compatibility Jamo, basic Latin and punctuation, every character of ko.json.
  Gaps:    syllables outside KS X 1001 (e.g. 똠, 힣) were cut; the font has no • (U+2022).

hom-chalk-latin.ttf     Pangolin Regular                      (family "Pangolin")
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/pangolin/Pangolin-Regular.ttf
  Licence: Pangolin-OFL.txt
  Kept:    Latin, Latin-1, Latin Extended-A/B, Latin Extended Additional, combining marks,
           Cyrillic (U+0400-052F), punctuation and symbols; all layout features.
           Covers en, de, es, fr, it, pl, pt-br, tr, ru.
  Gaps:    the font has no arrows (U+2190-21FF).

hom-chalk-ja.ttf        Klee One SemiBold                     (family "Klee One SemiBold")
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/kleeone/KleeOne-SemiBold.ttf
  Licence: KleeOne-OFL.txt
  Kept:    Hiragana, Katakana (+ phonetic extensions), CJK symbols and punctuation, half/full-width
           forms, the Joyo kanji (2,140 code points marked kJoyoKanji in Unicode's Unihan database:
           the 2,136 Joyo characters including U+20B9F, plus the 4 variant code points Unihan lists
           - U+5265, U+53F1, U+586B, U+982C; all present), every character of ja.json, Latin.
  Gaps:    the font has only 29 Latin Extended-A letters.

hom-chalk-zh-cn.ttf     Xiaolai Regular (formerly "Xiaolai SC", renamed upstream in v3.121)
                                                               (family "Xiaolai" / 小赖字体)
  Source:  https://github.com/lxgw/kose-font/releases/download/v3.126/Xiaolai-Regular.ttf
  Licence: Xiaolai-OFL.txt (https://raw.githubusercontent.com/lxgw/kose-font/master/OFL.txt)
  Kept:    GB2312 level-1 hanzi (3,755, bytes 0xB0A1-0xD7F9; all present), CJK punctuation,
           full-width forms, every character of zh-cn.json, Latin.

hom-chalk-zh-tw.ttf     Iansui Regular (芫荽)                  (family "Iansui")
  Source:  https://raw.githubusercontent.com/google/fonts/main/ofl/iansui/Iansui-Regular.ttf
  Licence: Iansui-OFL.txt (copied unchanged; its copyright line says 2022, the font's own name
           table says "Copyright 2025 The Iansui Project Authors" - the same OFL 1.1, the source
           repository simply did not update the year in OFL.txt)
  Kept:    3,945 common traditional characters, all of them Big5 level-1 characters. A full
           Big5 level-1 subset (5,401 characters) was 4.0 MB, and Big5 order is stroke-count order
           (cutting it would drop common characters such as 臺 灣 學), so the list was derived from
           Unicode's Unihan data instead: the traditional forms (kTraditionalVariant) of the 3,755
           GB2312 level-1 characters, plus the Hong Kong school grade-level characters
           (kGradeLevel), plus Big5 level-1 characters that are semantic/z/simplified variants of
           those (adds Taiwan standard forms such as 汙 佔 妳 祐 鑑), limited to Big5 level-1.
           The font has 3,943 of them (it lacks 阬 U+962C and 輥 U+8F25). Also Bopomofo
           (U+3100-312F, U+31A0-31BF) and tone marks, CJK punctuation, full-width forms, every
           character of zh-tw.json, Latin.
  Gaps:    Big5 level-1 characters outside the list above are not included; some are still
           seen in names (e.g. 妃 圳 夭 叩 吒 妍 伽).

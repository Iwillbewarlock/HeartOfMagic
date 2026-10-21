# Black Flame lantern - in-game text

**2026-10-21: the journal (BOOK 0x814) is removed from the ESP and the placement** - the user leaves the story to the
player's imagination. Its text below is kept only as a record; nothing in the game shows it.

First draft: Gemini Argon (`gemini-4-argon-high`, 2026-10-21) from the fact sheet in the design doc, section 6.
Checked line by line by Claude against the fact sheet. Changes from the draft: the descriptions now say the
resistance-ignoring part is *extra* damage (the spell itself is still resisted); "4E 175" written as the Korean game
writes dates ("제4시대 175년"); "매장실" -> "매장묘실" (the cell's Korean name); one English sentence with no Korean
counterpart and no fact behind it was cut (year 22); "Year N since" -> "The Nth year since".

Picked (user 2026-10-21): item name #1 사엘의 철창 등불 / Saeel's Iron Cage Lantern. The worn effect (MGEF 0x810) and
the old stoke buff (0x802/0x803) are named 갈망 / Longing (user 2026-10-21; "흑염 달구기" retired).

## Item name candidates

| # | Korean | English | Idea |
|---|---|---|---|
| 1 | 사엘의 철창 등불 | Saeel's Iron Cage Lantern | the dead husband's army lantern, glass and candle gone |
| 2 | 흑염의 등불 | Lantern of Black Flame | the wickless black flame itself, vanilla-style name |
| 3 | 흑염의 족쇄 | Shackle of Black Flame | no longer a light, a shackle worn at the belt |

## Item description (MGEF `HoMBF_LanternEffect` description)

| # | Korean | English |
|---|---|---|
| 1 | 착용 중 화염 마법이 흑염으로 타오르며 매지카 소모량이 30% 증가합니다. 화염 마법이 명중하면 저항을 무시하는 추가 피해로 대상 최대 체력의 2%(단발 시전) 또는 초당 최대 1.6%(집중 시전)를 줍니다. | While worn, fire spells burn as black flame and cost 30% more magicka. Fire spells that hit deal extra damage that ignores resistance: 2% of the target's maximum health per hit for single-cast spells, or up to 1.6% per second for concentration spells. |
| 2 | 화염 마법이 흑염으로 변하며 매지카를 30% 더 소모합니다. 화염 마법이 명중하면 저항을 무시하는 추가 피해를 주며, 단발 시전은 타격당 대상 최대 체력의 2%, 집중 시전은 초당 최대 1.6%입니다. | Fire spells burn as black flame and cost 30% more magicka. Fire spell hits deal extra damage that ignores resistance: 2% of maximum health per hit for single-cast spells, up to 1.6% per second for concentration spells. |
| 3 | 착용 시 화염 마법이 흑염을 일으키며 매지카 소모량이 30% 증가합니다. 명중한 화염 마법은 저항을 무시하는 추가 피해로, 단발 시전은 타격당 최대 체력의 2%, 집중 시전은 초당 최대 1.6%를 입힙니다. | While worn, fire spells burn as black flame and cost 30% more magicka. Fire spells that hit deal extra damage that ignores resistance: 2% of maximum health per hit for single-cast spells, up to 1.6% per second for concentration spells. |

## Book `HoMBF_LuahJournal`: 루'아의 그을린 연구 일지 / Lu'ah's Scorched Research Journal

### Korean

**제4시대 175년**

사엘이 죽었다. 알드메리에게서 제국 수도를 탈환하려는 전투에서 쓰러졌다고 했다. 내 품으로 돌아온 건 그의 유품 몇 가지와 편지 한 장뿐이었다. 〈제국군의 조의〉. 제국 군사장관 피우스 브룩시우스의 서명이 적힌 그 종이에는 사엘의 용기를 칭송하는 문장들이 줄지어 있었다. 종이 위의 칭송 따위가 죽은 내 남편을 돌려줄 수나 있는 것처럼. 편지 어디에도 등불에 대한 말은 없었지만, 집으로 보내온 사엘의 유품 속에는 그가 쓰던 제국군 등불이 함께 들어 있었다. 차갑고 투박한 철창 등불이었다. ... 나는 편지를 옆에 던져 두고, 빈 철창만 무릎 위에 올려놓은 채 밤을 지새웠다.

**그로부터 5년째**

《매니마코, 벌레들의 왕》이라는 시집을 글자가 닳도록 읽고 또 읽었다. 그 책 어디에도 어두운 불꽃을 다루는 주문의 이름이나 알려진 비술 따위는 적혀 있지 않다. 벌레들의 왕을 묘사한 단 한 구절, "그의 눈은 어두운 불꽃처럼 빛났고"라는 시구가 어두운 불꽃을 언급한 전부다. 시인이 남긴 한 줄을 단서 삼아 어두운 불꽃을 떠올린 건 온전히 나만의 해석이자 가설에 불과하다. 하지만 그 한 줄이 머릿속을 떠나지 않는다. 만약 내가 그 구절을 내 방식대로 풀어내어 어두운 불꽃을 피워 낼 수만 있다면, 사엘의 등불에 그 불을 담아 망자의 길을 밝힐 수 있지 않을까. 어둠 속을 헤매고 있을 사엘을 찾아내어 다시 일으킬 수만 있다면. ...

**그로부터 14년째**

사엘의 철창 등불에서 유리와 양초를 전부 뜯어내 버렸다. 내가 만들 불꽃에는 유리도 심지도 필요 없다. 텅 빈 철창 안에 심지 없이도 허공에 떠서 타오르는 흑염을 가두었다. 이 불꽃은 내 매지카를 연료로 삼아 타오른다. 불꽃을 유지하는 내내 내 안의 매지카가 빨려 나가는 게 느껴진다. ... 이건 더 이상 빛을 내는 도구가 아니다. 철창 안에서 흑염이 일렁여도 방 안은 조금도 밝아지지 않는다. 나는 불꽃을 가둔 이 철창을 마치 족쇄처럼 허리춤에 차고 다닌다.

**그로부터 22년째**

안질푼트의 매장묘실에서 나는 여전히 죽은 남편을 되살리는 데 매달리고 있다. 하지만 기나긴 세월 동안 내 슬픔은 증오와 광기로 썩어 들어갔고, 철창 안의 흑염 또한 나와 함께 변질되었다. 처음에는 분명 사엘을 찾기 위해, 죽은 자의 길을 밝혀 주려고 만든 등불이었다. ... 그러나 이 불꽃은 더 이상 망자를 인도하지 않는다. 이제 내 불꽃은 산 자에게 닿자마자 흑염으로 타오르며, 어떤 방어도 무시한 채 그 생명을 도려낸다.

**그로부터 26년째**

제4시대 175년 이후 스물여섯 해가 흘렀다. 허리춤에 족쇄처럼 매달린 빈 철창이 내 매지카를 집어삼키며 흑염을 키운다. 망자의 길을 밝히는 불이든 산 자의 목숨을 도려내는 불이든... 이제는 어느 쪽이든 상관없다.

### English

**4E 175**

Saeel is dead. He died in the battle to retake the Imperial City from the Aldmeri, and all that came home to me were his belongings and a letter. "Imperial Condolences," it is titled, signed by Imperial Praefect Pius Bruccius. The letter praises his courage, as though praise could bring my husband back. It mentions no lantern at all, yet among Saeel's belongings sent home was his Imperial army lantern, an iron cage lantern. ... I sit holding the cold iron cage while the letter lies beside me.

**The fifth year since**

I have read the poem *Mannimarco, King of Worms* until every line is etched into my mind. In the whole poem, only one verse speaks of "dark fire," saying of the Worm King: "His eyes burning dark fire." It is neither a named spell nor a known art. What I have built from that single verse is entirely my own reading, my own theory born of grief. Yet I take that line as a clue. If I can shape my own dark fire inside Saeel's lantern, perhaps it will light the way for the dead so I can find Saeel. ...

**The fourteenth year since**

I removed the glass and the candle from the iron cage. Inside the empty cage I now keep a black flame that floats and burns with no wick, burning my own magicka as its fuel. Every hour I sustain it, I feel it drawing upon my magicka to stay suspended between the iron bars. ... It is not a light source any more; it does not light the room. I wear it at my belt like a shackle that holds the flame.

**The twenty-second year since**

In Ansilvund's burial chambers I still try to raise my dead husband, yet over the years my grief has turned to hatred and madness. The black flame has changed with me. I meant it as a lantern to light the way for the dead, to find Saeel. ... Now it no longer guides the dead; it cuts out the life of the living and ignores their defenses.

**The twenty-sixth year since**

Twenty-six years since 4E 175. Worn at my belt like a shackle, the empty cage drinks my magicka to feed the black flame. Whether it guides the dead or cuts out the life of the living... I no longer care which.

Argon naturalness review (2026-10-21): 11 journal sentences flagged, 0 in the item description; 11 changed (one kept the in-game place name 매장묘실 and changed only the verb).

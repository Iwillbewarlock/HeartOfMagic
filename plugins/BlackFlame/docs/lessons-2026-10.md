# 흑염 작업에서 알게 된 것 (2026-10-15 ~ 10-20)

Skyrim AE 1.6.1170, CommonLibSSE-NG, MO2(TAKEALOOK) 기준. 주소는 모두 RVA이고 Address Library ID를 함께 적었다.
"확인"은 디스어셈블·로그·테스트로 본 것, "추정"은 아직 확인하지 않은 것이다. 근거 파일은 각 항목 끝에 적었다.
코드 구조는 [reuse-map.md](reuse-map.md), 수치 한도는 [checks-and-limits.md](checks-and-limits.md)에 있다.

## 1. 엔진 렌더링 사실

### 마법 효과 셰이더 인스턴스(ShaderReferenceEffect, SRE)

- **텍스처 멤버 (확인).** SRE의 텍스처는 Init 34912가 EFSH 경로로 비동기 요청해 SRE 멤버에 넣는다. 멤버는 다섯 개다.

  | SRE 멤버 | 내용 | EFSH 필드 |
  |---|---|---|
  | `+0xD0` | particle base | ICO2 |
  | `+0xD8` | particle palette | NAM9 |
  | `+0xE0` | membrane fill | ICON |
  | `+0xE8` | holes | NAM7 |
  | `+0xF0` | membrane palette | NAM8 |

  `34931` 요청이 끝나면 34933이 `34934`(attach)를 부른다. 호출 지점은 34933+0x12D(5D1B8D) 한 곳뿐이다. 그래서 이 지점에서 멤버를 NiPointer 대입으로 바꾸면, 원본 EFSH를 건드리지 않고 그 인스턴스 하나만 바뀐다(dtor 34911이 이 멤버들을 해제한다).
- **파티클 property 공유 (확인).** 파티클 property는 기본적으로 공유된다. 17325(호출 지점 34934+0x2EC, 5D1F1C)는 `share=true`일 때 `TESEffectShader+0x210/+0x218`에 property를 캐시해, 같은 EFSH의 모든 SRE가 그것을 함께 쓴다. OwnedController는 `share=true`이고, 이 경로가 FEC의 시체 효과다. 그래서 파티클 쪽을 바꿀 때는 `share=false`를 강제해야 NPC 효과로 번지지 않는다.
- **membrane 데이터 (확인).** membrane 쪽 BSEffectShaderData는 17328이 SRE마다 새로 만든다(인스턴스 소유).
  - 17332가 매 프레임 fill 색·알파·UV를 기록 색 키로 덮어쓴다. 텍스처(`+0x10/+0x18/+0x20`)는 건드리지 않는다.
  - Pop 34925는 SRE 멤버로 데이터를 다시 채운다. 그래서 BSEffectShaderData만 바꾸면 Pop에서 주황으로 돌아가고, SRE 멤버를 바꾸면 유지된다.
- **SRE flags bit 24 (확인).** 엔진의 어느 함수도 이 비트를 쓰거나 읽지 않는다. .pdata 함수 120,760개를 스캔해 확인했다.
  - ctor 34910이 `+0x130`을 0으로 대입하므로 표시는 ctor가 돌아온 뒤에 해야 한다.
  - 저장 34920은 flags를 그대로 저장하고, 로드 34921은 `& ~0x18`만 하므로 비트가 살아남는다.
- **원본 경로 재요청 (미확인).** `34932`는 원본 텍스처 경로를 다시 요청하는 함수지만 직접 호출자가 0개다. "엔진이 원본 경로를 다시 읽지 않는다"는 절대 명제로는 쓰지 않는다.

근거: 세션 scratchpad `sre-instance-findings.md`, `build/fec-base/engine/ENGINE.md`.

### 그리기 순서

- **NiAlphaProperty 비트 (확인).** 비트 10-12는 alpha test 함수다. No_Sorter(비트 13, `0x2000`)는 Skyrim 이펙트 경로가 읽지 않는다. GECK 위키 설명은 Fallout 문헌이다.
- **BSOrderedNode (확인).** 같은 NIF 안에서 순서를 고정하는 방법이다.
  - 자식은 배열 순서대로 그려진다. 1.6.1170의 근거는 전방 순회 D1E310, 그룹 tail append 14F1877, FIFO draw 14F1ACD다.
  - 노드 자체는 다른 물체와 bound로 정렬된다. 손처럼 움직이는 대상은 동적 bound(flags `0x8000E`)를 쓴다.
  - 로드 순서의 불 NIF 47개가 이 노드를 쓴다.
  - "연기를 먼저 그린다"는 보편 규칙이 아니다.
- **additive와 alpha의 차이.** additive는 순서와 무관하다(더하기만 한다). alpha 블렌드는 뒤에서 앞으로 그려야 한다.

근거: `build/ordered/REPORT.md`, `build/ordered/engine/EVIDENCE.md`.

### 입자 모디파이어 의미 (SSE 1.7.104에서 확인, 1.6.1170은 추정)

| 모디파이어 | 의미 |
|---|---|
| BSPSysSubTexModifier `Frame Count` | 초당 프레임 수(FPS)가 아니라 **수명 동안 바뀌는 칸 수**다. |
| BSPSysSubTexModifier 시작 칸 | `trunc(startFrame + U × startFrameVariation)`이고, U는 float 반올림으로 1.0이 될 수 있다. 칸이 N개면 variation은 `N - ε`로 둬야 마지막 칸도 나오고 N은 나오지 않는다. |
| 이미터 수명 | `life + (U - 0.5) × variation`(전체 폭) |

근거: `build/hand-motion/subtex-engine-disassembly.txt`, `nif-conventions.txt`.

### 룬 바닥 문양

- **(확인)** 발사 직후 바닥 문양은 일반 충돌 경로를 거치지 않는다. 경로는 MagicCaster helper AE34452 +0x52F(5C028F) → AE44186 → ApplyDecal AE15203이다.
  - TXST는 `MGEF.impactDataSet → IPDS → IPCT.decalTextureSet`에서 온다. 조회 지점은 7E9C95 → AE20860이다.
  - 복제 PROJ의 decalData는 이 경로에서 쓰이지 않고, 셀 데칼 재적용(AE19575)에서만 쓰인다.
- **(확인)** 1.6.1170 전용 훅으로 처리했다. 10회차 화면에서 회색을 확인했다.

근거: `build/rune-decal/REPORT.md`.

## 2. 데이터·텍스처 사실

- **FEC 시체 불 (확인).** FEC 시체 불은 EFSH 6개로 이루어지고, ApplyEffectShader OwnedController로 붙는다. 종류는 두 가지다.
  - 팔레트를 쓰는 것(800, 806, 826, 8E2 등): 팔레트만 바꾸면 회색이 된다.
  - 텍스처 색을 그대로 쓰는 것(burnshader, animalburnshader): 바탕 텍스처를 바꿔야 한다.

  그을음·연기(sootshader, FXSmokeSoftSub, FXthinSpinBitAnim01)는 원래 무채색이라 대상이 아니다.
- **불 판정 기준 (확인).** alpha 가중치를 준 빨강~노랑 비율로 판정하며, 기준은 5%다. 조건은 S ≥ 0.45, V ≥ 0.30, 주황~노랑 15-55°이고, 빨강은 V ≥ 0.55일 때만 센다.

  | 텍스처 | 비율 |
  |---|---|
  | burnshader | 23.01% |
  | animalburnshader | 11.27% |
  | 불 아틀라스 | 94.87% |
  | 피·연기·서리 등 | 0.008% 이하 (피의 빨강은 V 최고 0.451) |
- **BC7 재압축 (확인).** BC7로 다시 압축하면 alpha가 최대 140/255까지 바뀐다. 그래서 회색 사본은 RGBA8, 전체 mip, alpha 바이트 그대로로 저장하고, 다시 읽어 R=G=B와 alpha 일치를 검사한다.
- **Glow Be Gone Redone NG (확인).** `RemoveActorFX=true`이면 kDataLoaded에서 거의 모든 MGEF hit shader를 null로 만든다. Partial Form Patcher는 10F7F1(Incinerate)만 되살린다. 사용자 결정에 따라 이 동작을 존중한다.
- **SRE vtable (확인).** 다른 DLL이 이미 가로챈 슬롯이 있다.

  | 슬롯 | 가로챈 DLL |
  |---|---|
  | 0x27, 0x36, 0x37, 0x38, 0x3B | po3_LightPlacer |
  | 0x28, 0x3E | MultiEffectShader |

  그래서 vtable 훅 대신 호출 지점 write_call을 썼다.
- **GetTexture (확인).** 없는 파일을 요청하면 `BSShader_DefNormalMap` 대체 텍스처를 돌려준다. 결과가 진짜 텍스처인지 판별해야 한다.

## 3. 시스템·도구 사실

- **빌더 메모리 대기 (확인).** 빌더가 메모리 부족으로 기다리는 원인은 시스템 commit 부족이었다(배경 commit 약 29 GB). 빌더는 4096 MiB 여유(GreyBudget Admit)를 지키도록 설계됐고, 게임 전에 앱을 닫으면 대기가 없다(9회차).
- **usvfs 실제 경로 (확인).** usvfs 아래에서 Data 경로의 실제 위치는 `GetFinalPathNameByHandleW`로 알 수 있다. 옛 사본 정리는 실제 경로가 overwrite 아래일 때만 지운다.
- **nifly (확인).** ref 배열은 non-const로만 순회된다. nifly의 블록 삭제는 블록 번호를 다시 매기는데, 저장된 blend interpolator 자식은 참조 열거에서 빠진다. 그래서 고아 블록은 지우지 않고 남겼다.

## 4. 연출 원칙 (사용자 결정)

- **흑염의 생김새.** 밝은 심지에 어두운 부분이 섞인 모양이다(사용자 정정 2026-10-20: "바깥이 희고 안이 검다"가 아니라 그 반대). additive만으로는 어두운 색을 칠할 수 없어서, alpha 블렌드 검은 마블 층을 따로 둔다.
- **손 불꽃.** 주문마다 바뀌지 않으므로 하나의 레시피(변형 B: 마블 불투명도 0.9, 크기 1.15)로 둔다.
- **NPC 불.** 바뀌지 않는다. 흑염으로 죽은 시체를 NPC가 다시 태우면 그 불은 주황으로 유지된다.

## 5. 작업 방식에서 배운 것

- **사용자 인게임 확인.** 확인 항목을 한 번에 묶고, 검토는 2번(리뷰어 에이전트 + GPT) 한다. 이번에는 10회차까지 실행했다.
- **GPT(Codex companion).**
  - 병렬 작업은 각자 stdout을 파일로 받아야 한다. `result`는 섞인다.
  - 물어볼 것이 생겨 멈추면 `task --resume-last`로 이어 간다.
  - 수정 금지 파일에 걸리면 멈추고 묻는다. 그래서 금지 범위를 정확히 적어 준다.
  - FormID를 하드코딩하는 제안을 한 적이 있다. 범용 규칙을 요구 사항에 명시한다.
- **Gemini(agy) 헤드리스.** 파일을 읽지 못하고, 도구 실행은 거부된다. 그래서 입력은 프롬프트에 넣고 "도구 실행 금지"를 적는다. 웹 조사는 15분 제한에 걸리므로 질문을 짧게 하고 `--print-timeout 1500s`를 준다. 2026-10-20부터 Gemini는 검토를 하지 않는다.
- **에이전트 둘이 같은 파일을 고칠 때.** 파일 범위를 나눠 주고, 끝난 뒤 합쳐 빌드해 겹친 파일에서 빠진 수정이 없는지 리뷰어가 따로 확인한다.
- **로그로 먼저 판정한다.** 사용자가 "다 된다"고 해도 로그 수치를 같이 본다. 10회차에서 바탕 텍스처 교체 경로는 한 번도 발동하지 않았다는 것을 로그로 알았다.

## 6. 아직 확인하지 않은 것

- FEC 바탕 텍스처 교체(burnshader fill)의 실제 발동과 화면 모양.
- 같은 시체에서 회색 SRE와 주황 SRE가 겹칠 때(GetStackableMatch 34927, Push/Pop) 어느 쪽이 남는지.
- 입자 모디파이어 의미를 1.6.1170 바이너리에서 직접 확인하는 것.
- 1.6.1170 밖의 런타임(SE 1.5.97, VR): 룬 창과 시체 훅은 설치되지 않는다.

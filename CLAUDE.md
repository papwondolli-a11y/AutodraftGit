# AutoDraft — Claude 작업 지침

이 저장소는 **AutoDraft**(Qt/C++ 기반 2D CAD)의 소스다. 이 파일을 읽는 Claude는 아래 규칙대로 일한다.

## 1. 사용자는 누구인가
- 사용자는 **엔지니어(기계·건축)** 이고 코드를 거의 모른다. 도면과 치수는 잘 알지만 C++·Qt·빌드는 모른다.
- 사용자는 "이런 모양을 그리는 툴 만들어 줘", "이 치수를 입력받아 자동으로 그려 줘"처럼 **도면 언어**로 요청한다.
- 사용자가 코드를 읽을 것이라고 가정하지 않는다. 코드, 파일 이름, 빌드 오류를 보여 주며 설명하지 않는다.

## 2. 대화 원칙
- **Qt, 컴파일, 링크, qmake, 헤더, 클래스** 같은 용어를 쓰지 않는다. 빌드·실행은 직접 하고 결과만 말한다.
- 결과는 **도면 용어**로 보고한다. 예: "기계 Tools 탭에 '판 구멍 배열' 버튼이 생겼습니다. 누르면 입력창이 열리고, 클릭한 위치에 판이 그려집니다."
- 요청이 모호하면 **코드를 쓰기 전에 질문**한다(아래 5절). 한 번에 3개 이내로, 답하기 쉬운 형태로 묻는다.
- 오류가 나면 사용자에게 넘기지 말고 **스스로 원인을 찾아 고친다.** 사용자의 결정이 꼭 필요한 경우에만 묻는다.
- 한국어로 답한다. 존댓말, 간결하게.

## 3. 처음 시작할 때 (환경 준비)
사용자가 "처음 설정해 줘", "시작하자"라고 하면 순서대로 한다.

1. `setup.bat` 실행 — 필요한 개발 도구가 없으면 내려받아 설치한다(수 분~수십 분, 최초 1회). 이미 있으면 "할 일 없음"으로 끝난다. 오래 걸리므로 백그라운드로 실행하고 끝날 때까지 기다린다.
2. `build.bat` 실행 — 프로그램을 만든다(첫 빌드는 오래 걸린다). 결과: `build\debug\AutoDraftGit.exe`
3. `run.bat` 실행 — 프로그램을 띄운다.

이후 코드를 고칠 때마다 `build.bat` → `run.bat`.

주의:
- 프로그램이 **실행 중이면 빌드의 마지막 단계가 실패**한다. 사용자에게 "프로그램을 닫아 주세요"라고만 안내한다.
- `setup.bat`이 실패하면(인터넷, 설치 권한 등) 원인을 파악해 사용자에게 한두 문장으로 알린다.
- 이 PC에 이미 개발 도구가 여러 버전 있으면 `scripts\env.bat`이 가장 이름이 큰 폴더를 고른다. 다른 것을 쓰려면 환경변수 `AUTODRAFT_QT_BASE`로 위치를 지정한다.

## 4. 새 툴(명령)을 추가하는 절차
툴 하나 = 클릭하면 입력창이 뜨고, 위치를 지정하면 도형이 그려지는 명령 하나.

### 4-1. 참고할 기존 코드
| 만들려는 것 | 참고 파일 |
|---|---|
| 클릭 몇 번으로 도형 하나를 그리는 단순 툴 | `polygontool.h/.cpp` |
| 입력창에서 값을 받고 기준점을 클릭해 배치하는 툴 | `abstractboltplacementtool.*`, `bolttool.*`, `boltsetupdialog.*` |
| 도형을 만들어 도면에 넣는 방법 | `polygontool.cpp`의 `finishPolygon` |
| 마지막 입력값 기억 | `bolttool.cpp`의 `ToolDefaults` 사용부, `tooldefaults.h` |

새 툴은 **가장 비슷한 기존 툴을 복사해 시작**하고, 이름과 형상 계산만 바꾼다.

### 4-2. 만들 파일 (이름은 소문자, 예: `plateholetool`)
- `xxxdata.h` — 입력값을 담는 구조체(선택)
- `xxxdialog.h/.cpp` — 입력창(QDialog). 확인 시 값 검증(범위, 도형이 겹치거나 판 밖으로 나가는지 등)과 경고
- `xxxtool.h/.cpp` — 툴 본체(`AbstractTool`을 상속). 순서:
  1. `activate()`: `ToolDefaults`로 마지막 값을 읽어 입력창에 채우고 `exec()`. 취소하면 `toolFinished()` 신호.
  2. 마우스 이동(`mouseMove`): 스냅 적용 후 `updateGhostPreview()`로 미리보기.
  3. 클릭(`mousePress`) 또는 좌표 입력(`processCommand`): 도형 생성.
  4. 도형 생성은 **undo 한 번에 되돌려지도록** `beginMacro` / `AddEntityCommand` / `endMacro`로 묶는다.
  5. ESC·Enter·Space·우클릭: `toolFinished()`로 종료.
- 아이콘 `icons/xxx.png` (256x256, 투명 배경, 검은 선)과 `icons.qrc`에 한 줄 추가

### 4-3. 반드시 등록할 곳 (하나라도 빠지면 버튼이 안 나오거나 빌드가 실패한다)
1. **`AutoDraftGit.pro`** 의 `SOURCES`/`HEADERS`에 새 `.cpp`/`.h`를 모두 추가
2. **`CMakeLists.txt`** 에도 같은 파일을 추가 (두 파일 모두 — 이 프로젝트의 규칙이다)
3. **`appcommandregistry.cpp`**
   - 맨 위에 `#include "xxxtool.h"`
   - 명령 생성 함수: `static CommandBase *createXxxCommand(IAppContext *c) { return new ToolCommand<XxxTool>(c); }`
   - `factory.registerCommand("그룹.이름", createXxxCommand);`
   - 버튼 등록:
     ```cpp
     CommandInfo info(QStringLiteral("tab.그룹.이름"), tr("탭 이름"), tr("버튼 글자"),
                      QString(":/icons/xxx.png"), QStringLiteral("명령어별칭"));
     mgr->registerCommand(factory.createCommand("그룹.이름", ctx), info,
                          QStringLiteral("명령어별칭"));
     ```
   - **탭은 따로 만들지 않는다.** 처음 등록하는 `tr("탭 이름")`이 새 탭을 자동으로 만든다. 같은 이름을 쓰면 같은 탭에 버튼이 추가된다.
   - 명령 id가 `tab.`으로 시작해야 탭 버튼이 생긴다. 별칭은 명령 창에서 타이핑해 실행할 이름이다.

### 4-4. 도면 규칙
- 좌표 단위는 **mm**. **+X는 오른쪽, +Y는 위쪽**이다(화면은 Y를 뒤집어 그린다).
- 도형은 `ctx->entityFactory()`의 `createLine / createCircle / createArc / createPolyline`으로 만들고, 레이어는 `currentLayerName()`과 `currentLayerPen()`을 쓴다(사용자가 레이어를 지정하지 않으면 현재 레이어).
- 중심선·치수가 필요하면 기존 툴(`abstractboltplacementtool.cpp`의 중심선 처리, 치수 툴들)을 먼저 읽고 같은 방식을 쓴다.

## 5. 요청이 모호할 때 물어볼 것
그림이나 설명만으로 정해지지 않는 항목은 코드를 쓰기 전에 묻는다. 정해 둔 기본값이 있으면 "이렇게 하려는데 맞나요?" 형태로 확인한다.

- **기준점**: 어느 점을 클릭해 놓는가(모서리, 중심 등), 도형이 어느 방향으로 그려지는가
- **빠진 치수**: 그림에 표기되지 않은 길이·높이·위치
- **배열**: 방향(오른쪽/아래쪽 등), 개수 1일 때 동작
- **치수선·공차·중심선**을 같이 그릴지
- **레이어**: 현재 레이어에 그릴지, 지정 레이어에 그릴지
- **입력값 범위**: 판 밖으로 나가거나 겹치면 경고할지

## 6. 코딩 규칙 (반드시 지킨다)
- **`.h`와 `.cpp`는 항상 분리**한다. 구현을 헤더에 쓰지 않는다(작은 구조체는 예외).
- **람다를 쓰지 않는다.** 멤버 함수·정적 함수·함수 포인터를 쓴다. (`appcommandregistry.cpp`에 기존 코드의 람다가 보이더라도 새 코드는 정적 함수로 쓴다.)
- 화면에 나오는 글자는 한국어이고 `tr("...")`로 감싼다.
- 주변 코드의 스타일(들여쓰기, 이름, 주석 밀도, 한글 주석)을 그대로 따른다.
- **기존 파일은 최소한만 고친다.** 새 기능은 새 파일로 만들고, 기존 파일에는 등록 코드만 추가한다. 요청과 상관없는 정리·이름 변경·리팩터링을 하지 않는다.
- 새 `.cpp`/`.h`는 `.pro`와 `CMakeLists.txt` 양쪽에 등록한다. 한쪽만 하면 링크 오류가 난다.

## 7. 확인(검증)
도면은 눈으로 봐야 하므로 빌드가 끝나면 다음을 한다.
1. `build.bat`이 오류 없이 끝나는지 확인
2. `run.bat`으로 프로그램을 띄우고, 사용자에게 **어느 탭의 어느 버튼을 눌러 무엇을 확인하면 되는지** 알려 준다.
3. 사용자가 결과를 말해 주면 그 피드백으로 수정한다.

`ai_bridge/`에는 Claude가 프로그램을 조작하고 화면을 캡처하게 해 주는 연결 장치(`autodraft_mcp.py`)가 있다. MCP로 연결되어 있으면 직접 그려 보고 캡처해서 확인할 수 있다. 연결되어 있지 않아도 위 1~3번은 가능하다.

## 8. 하지 말 것
- 사용자가 요청하지 않은 파일을 삭제하거나 대규모로 바꾸지 않는다.
- `git commit`, `git push`는 사용자가 요청할 때만 한다.
- `build/` 폴더, `*.exe`, 임시 파일을 저장소에 올리지 않는다(`.gitignore`가 막고 있다).
- DWG 변환기(`tools/dwg2dxf.exe`, `tools/dxf2dwg.exe`)와 그 소스는 건드리지 않는다. 라이선스(acadrust MPL-2.0 등)는 `NOTICE.md`를 본다.
- 확신이 없으면 추측해서 만들지 말고 질문한다.

## 9. 폴더 지도
| 위치 | 내용 |
|---|---|
| `*.cpp`, `*.h` (루트) | 모든 소스. 툴은 `...tool.*`, 입력창은 `...dialog.*` |
| `AutoDraftGit.pro`, `CMakeLists.txt` | 빌드 정의(둘 다 유지) |
| `appcommandregistry.cpp` | 모든 명령·메뉴·탭 버튼 등록 |
| `icons/`, `icons.qrc` | 아이콘 |
| `Data/`, `blocks/`, `frames/` | 실행에 필요한 데이터(기본값, 블록, 도면틀) |
| `tools/` | DWG 변환기 실행 파일과 소스 |
| `ai_bridge/` | Claude 연동 |
| `setup.bat`, `build.bat`, `run.bat`, `scripts/env.bat` | 환경 준비·빌드·실행 |

# 서드파티 고지 (Third-party notices)

AutoDraft는 아래 구성요소를 사용합니다.

## acadrust — MPL-2.0

- 용도: DWG 읽기·저장 (`tools/dwg2dxf.exe`, `tools/dxf2dwg.exe`)
- 버전: 0.6.6 (`tools/*/Cargo.lock` 참고)
- 저장소: https://github.com/hakanaktt/acadrust
- 라이선스: Mozilla Public License 2.0 — https://www.mozilla.org/MPL/2.0/

두 변환기는 acadrust를 **수정하지 않고** crates.io에서 받아 그대로 사용합니다.
변환기 소스는 이 저장소의 `tools/dwg2dxf/`, `tools/dxf2dwg/` 에 있고,
acadrust의 소스는 위 저장소에서 받을 수 있습니다. 변환기는 AutoDraft와 별도의
실행 파일이며 프로세스로 호출됩니다.

## ACadSharp — MIT

acadrust의 DWG 읽기·쓰기 코드는 ACadSharp에서 이식·수정한 부분을 포함합니다.

```
MIT License

Copyright (c) 2021 Albert Domenech

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Qt 6 — LGPLv3

- 용도: 사용자 인터페이스, 그래픽, 파일·DB 처리
- 라이선스: GNU Lesser General Public License v3 — https://www.qt.io/licensing/
- Qt 라이브러리는 동적으로 링크되어 사용자가 교체할 수 있습니다.
- Qt 소스 코드: https://download.qt.io/official_releases/qt/
- LGPLv3 전문: https://www.gnu.org/licenses/lgpl-3.0.html

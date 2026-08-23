# braille-drv8833 — 전자석 래치 점자 디스플레이

**2026 임베디드 소프트웨어 경진대회 출품작**의 점자 출력 모듈 펌웨어.
시각장애인을 위한 저가형 전자 점자 디스플레이를 목표로, 상용 피에조 셀 대신
ESP32-S3 + DRV8833 + 자작 코일(못 코어 + 네오디뮴 자석)로 점자 핀을 구동한다.

핵심 아이디어는 **쌍안정 자기 래치**: 짧은 펄스(기본 20ms)로 핀을 올리면
전원 0 상태로 매달려 유지되고, 반대 극성 펄스로 내린다. 유지 전력이 없어
배터리 구동에 유리하고, 셀당 부품비가 코일+하프브리지 반쪽 수준으로 내려간다.

현재 단계는 3×3 프로토타입 모듈 벤치 검증이며, 상위 제어기(Raspberry Pi)가
시리얼 패턴 프로토콜(`B:XXXXXXXXX`)로 이 모듈을 구동하는 구조다.
아래는 처음 세팅할 때 필요한 전 과정 (보드 인식 → PlatformIO 플래싱).

## 🙌 코딩 몰라도 됩니다 — 바이브 코딩으로 시작하기

이 저장소는 팀원이 코딩/ESP32를 몰라도 AI에게 시켜서 조작할 수 있게 만들어졌다.
직접 명령어를 칠 필요 없이 [Gemini CLI](https://github.com/google-gemini/gemini-cli)에게
한국어로 부탁하면 된다 (구글 계정만 있으면 무료).

1. **Node.js 설치** — https://nodejs.org 에서 LTS 버전 다운로드 → 계속 "다음"으로 설치
2. **Gemini CLI 설치** — PowerShell을 열고 아래 한 줄을 붙여넣기:
   ```powershell
   npm install -g @google/gemini-cli
   ```
3. **실행** — PowerShell에서 `gemini` 입력 → 브라우저에서 구글 계정 로그인
4. **첫 요청** — 아래 문장을 그대로 붙여넣기:
   > https://github.com/changhoon-yoon/braille-drv8833 클론해서 README대로 개발 환경을 세팅해줘.
   > 드라이버나 프로그램 설치가 필요하면 알려주면서 진행해줘.
5. **보드 연결 후** — USB로 보드를 꽂고 이렇게 시키면 끝:
   > 보드에 펌웨어 업로드하고, 전 코일 스윕 테스트(a) 돌려서 결과 알려줘

이후에도 전부 말로 하면 된다. 예시:
- "점자 패턴 `B:101000000` 보내줘"
- "펄스 세기를 5% 올려서 다시 테스트해줘"
- "업로드가 안 되는데 원인 찾아줘"

아래부터는 수동으로 세팅하고 싶은 사람(또는 AI가 참고할)을 위한 상세 가이드다.

## 하드웨어

| 부품 | 내용 |
|---|---|
| MCU | ESP32-S3 DevKitC (N16R8, 16MB Flash + 8MB PSRAM) |
| 드라이버 | DRV8833 듀얼 H브리지 모듈 ×5 (모듈당 코일 2개) |
| 코일 | 자작 — 못 코어에 에나멜선, 위에 네오디뮴 자석 핀 |
| 전원 | 모터전원(VM) 5~6V — 벤치 서플라이 권장 (전류 제한 기능이 고장을 잡아줌) |
| USB-시리얼 | 보드 온보드 CH343 (COM 라벨 포트) |

### 배선 (핀맵)

DRV8833 모듈 기준. 각 모듈 공통: VM=5~6V, GND=ESP32와 공통, **EN/SLP=3V3 (빼먹으면 전부 침묵)**.
VM 가까이에 470uF 전해캡 (긴 다리 = +).

| 모듈 | IN1(AIN1) | IN2(AIN2) | A출력 | IN3(BIN1) | IN4(BIN2) | B출력 |
|---|---|---|---|---|---|---|
| #1 | GPIO4 | GPIO5 | coil0 | GPIO6 | GPIO7 | coil1 |
| #2 | GPIO8 | GPIO9 | coil2 | GPIO10 | GPIO11 | coil3 |
| #3 | GPIO12 | GPIO13 | coil4 | GPIO14 | GPIO15 | coil5 |
| #4 | GPIO16 | GPIO17 | coil6 | GPIO18 | GPIO21 | coil7 |
| #5 | GPIO38 | GPIO39 | coil8 | — | — | 미사용 |

코일 배치는 행 우선: coil0=왼위 … coil8=오른아래. 코일은 OUT1/2(A) 또는 OUT3/4(B)에 연결.
극성이 반대면 밀기/당기기가 뒤바뀌므로 코일 두 선만 맞바꾸면 된다.

> ESP32-S3 핀 주의: GPIO26~37(플래시/PSRAM), 0/19/20/43~46(부트/USB), 48(온보드 LED)은 사용 금지.

## 1. ESP32 인식시키기 (Windows)

1. USB 케이블로 보드의 **COM(UART) 라벨 포트**를 PC에 연결 (데이터 지원 케이블일 것 — 충전전용 케이블이 의외로 흔한 함정)
2. 장치 관리자 → 포트(COM & LPT)에 **`USB-Enhanced-SERIAL CH343 (COMx)`** 가 보이면 인식 성공
3. 안 보이면 CH343 드라이버 설치: WCH 공식 [CH343SER 드라이버](https://www.wch.cn/downloads/CH343SER_EXE.html) 설치 후 재연결
4. PowerShell로도 확인 가능:
   ```powershell
   [System.IO.Ports.SerialPort]::GetPortNames()
   ```

**COM 번호는 USB 꽂는 자리마다 바뀔 수 있다** (이 프로젝트에서도 COM5→COM8 이력 있음).
그래서 `platformio.ini`에 포트를 고정하지 않고 자동 탐지에 맡긴다.

## 2. PlatformIO 설치

VSCode 없이 CLI만으로 충분하다:

```powershell
pip install platformio
```

설치 확인:

```powershell
pio --version
```

(`pio`가 PATH에 없으면 `C:\Users\<이름>\AppData\Local\Programs\Python\Python3xx\Scripts\pio` 전체 경로로 실행)

## 3. 빌드 & 플래싱

```powershell
git clone https://github.com/changhoon-yoon/braille-drv8833.git
cd braille-drv8833

pio run              # 빌드만 (첫 빌드는 툴체인 다운로드로 수 분 소요)
pio run -t upload    # 빌드 + 플래싱 (포트 자동 탐지)
```

특정 포트를 강제하려면:

```powershell
pio run -t upload --upload-port COM8
```

### 플래싱이 안 될 때

| 오류 | 원인 | 해결 |
|---|---|---|
| `Could not open COMx ... PermissionError(13)` | 다른 프로그램이 포트 점유 (Docklight, 시리얼 모니터 등) | 해당 프로그램에서 연결 해제 (Docklight는 F6) 후 재시도 |
| `Could not open COMx ... FileNotFoundError(2)` | 그 COM 번호에 보드가 없음 (USB 자리 바뀜/빠짐) | `GetPortNames()`로 현재 번호 확인, 케이블 재연결 |
| 아예 포트가 안 잡힘 | 드라이버 미설치 or 충전전용 케이블 | CH343 드라이버 설치, 케이블 교체 |

## 4. 시리얼 모니터

```powershell
pio device monitor    # 115200bps, Ctrl+C로 종료
```

또는 Docklight/기타 터미널로 115200bps 연결. **플래싱 전엔 반드시 연결을 끊을 것.**

## 5. 시리얼 명령

### 패턴 프로토콜 (상위 제어기 → 보드)

```
B:XXXXXXXXX\n     9자리 0/1, 행 우선 (왼위→오른아래)
B:101000000\n     coil0, coil2 올림 — 0 자리는 무조건 당김 펄스로 내림
B:000000000\n     전부 내림
```

'0' 자리는 상태 추적과 무관하게 매번 반대 극성 펄스를 발사하므로,
크로스토크로 어긋난 핀도 패턴 적용 때마다 물리적으로 재정렬된다.

### 벤치 튜닝 키

| 키 | 동작 |
|---|---|
| `0`~`8` | 대상 코일 선택 |
| `u` / `d` | 선택 코일 올리기 / 전부 내리기+모드 정지 |
| `p` / `q` | 밀기 펄스 / 당기기 펄스 |
| `a` | 전 코일 스윕 (0→8 PUSH → 대기 → 전부 PULL) |
| `t` / `r` | 자동 반복 펄스 / 연속 ON-OFF 교대 (60초 자동정지) |
| `o` | 100% 연속 ON (전류계 측정용, 발열 주의) |
| `+` `-` | 펄스폭 ±10ms (기본 20ms) |
| `(` `)` | 펄스 파워 ∓5% (기본 70%) |
| `<` `>` | 반복 간격 ∓250ms (기본 3000ms) |
| `h` | 래치/홀드 모드 전환 (기본 래치 = 유지전류 0) |
| `s` / `?` | 상태 / 도움말 |

## 개발 워크플로우

펌웨어 개발은 [Claude Code](https://claude.com/claude-code)를 활용한 바이브 코딩으로 진행했다
(요청 → 코드 수정 → 빌드 → 플래싱 → 커밋). 기능 단위로 커밋하며 커밋 메시지에 변경 이유를 남긴다.

### 로드맵 (경진대회)

- [x] 1코일 벤치: 래치 물리 검증, 최소 펄스폭 탐색 (20ms)
- [x] 펄스 래치 구동 전환 (유지전류 0) + 펄스 파워 % 조절
- [x] 9코일 패턴 프로토콜 (`B:XXXXXXXXX`) + 전 코일 스윕 테스트
- [ ] 3×3 모듈 전 셀 조립 및 스윕 검증
- [ ] 셀 간 자기 간섭(크로스토크) 대책 — 상단 철 와셔 래치 / 철판 격벽
- [ ] 상위 제어기(Pi) 연동 — 카메라 문자 인식 → 점자 패턴 출력

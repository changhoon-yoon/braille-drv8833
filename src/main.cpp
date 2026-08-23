// ============================================================
// 점자 모듈 펌웨어 (ESP32-S3 N16R8 + DRV8833) — 9코일 + 패턴 프로토콜
//
// [구조]
//   Pi 4 (카메라 인식) --USB 시리얼--> 이 펌웨어 --> DRV8833 x5 --> 코일 9개
//
// [배선 - 코일당 DRV8833 반쪽 1개]
//   DRV8833 VM      <- 외부 5V (모듈 가까이에 470uF, 긴다리=+가 5V)
//   DRV8833 GND     <- 전원 GND ── ESP32 GND 와 반드시 공통!
//   DRV8833 EN/SLP  <- ESP32 3V3 (이거 빼먹으면 전부 침묵)
//   AIN1/BIN1       <- 아래 COILS 표의 첫 핀 (밀기 방향)
//   AIN2/BIN2       <- 아래 COILS 표의 둘째 핀 (당기기/홀드 PWM)
//
// [ESP32-S3 N16R8 핀 주의]
//   GPIO 26~37 = 플래시/PSRAM 전용 → 절대 사용 금지
//   GPIO 0, 19, 20, 43, 44, 45, 46 = 부트/USB/UART → 회피
//   GPIO 48 = 데브킷 온보드 RGB LED → 회피
//
// [시리얼 프로토콜 - 115200bps, Pi에서 사용]
//   B:XXXXXXXXX\n : 9자리 0/1 패턴 적용 (행 우선: 왼위→오른위, ... →오른아래)
//                   예) B:110110000\n  응답: [OK] ...
//   B:000000000\n : 전부 내림
//
// [벤치 튜닝 명령 - 사람이 시리얼 모니터에서]
//   0~8 : 튜닝 대상 코일 선택
//   u : 선택 코일 올리기                  d : 전부 내리기 + 모든 모드 정지
//   h : 래치/홀드 모드 전환 (기본: 래치 — 펄스 후 전원 0으로 매달림 유지)
//   p : 밀기 펄스만    q : 당기기 펄스    o : 100% 연속 ON (전류계용, 발열!)
//   t : 자동 반복 펄스  r : 켜고-끄고 교대 (60초 자동정지)
//   a : 전 코일 스윕 (0→8 순차 PUSH → 대기 → 0→8 순차 PULL)
//   + / - : 펄스폭 ±10ms    [ / ] : 홀드 듀티 ∓10    < / > : 반복간격 ∓250ms
//   s : 상태  ? : 도움말
// ============================================================

#include <Arduino.h>

// ---------- 코일 핀 표 {밀기(IN1), 당기기/홀드(IN2)} ----------
// 행 우선 배치: 코일0=왼위, 코일1=중위, 코일2=오른위, ... 코일8=오른아래
// 코일0 = 기존 1코일 벤치 배선(GPIO 4,5) 그대로.
const int NUM_COILS = 9;
const int COILS[NUM_COILS][2] = {
  { 4,  5}, { 6,  7}, { 8,  9},
  {10, 11}, {12, 13}, {14, 15},
  {16, 17}, {18, 21}, {38, 39},
};

// ---------- LEDC: 홀드 전용 채널 1개를 전 코일이 공유 ----------
// S3는 LEDC 채널이 8개뿐이라 코일당 1채널은 불가능.
// 홀드 듀티는 전 코일이 같으므로, 홀드 중인 코일의 IN2만 이 채널에
// attach하고 팝/내리기는 digitalWrite로 처리한다.
const int CH_HOLD  = 0;
const int PWM_FREQ = 20000; // 20kHz: 사람 귀에 안 들림
const int PWM_RES  = 8;     // 듀티 0~255

// ---------- 튜닝 변수 (시리얼로 실시간 조정) ----------
int  pulseMs  = 20;   // 팝 펄스 폭 (래치 최소 통전 탐색 — 길면 반동으로 되튈 수 있어 짧게, +/-로 조정)
int  holdDuty = 70;   // 홀드 실효 듀티 70/255 ≈ 27%
int  popGapMs = 60;   // 순차 팝 사이 간격 (동시 발사 금지 — 전원 딥 방지)
bool latchMode = true; // true: 펄스 래치(유지전류 0, 20ms 검증됨) / false: 팝→홀드 PWM

bool up[NUM_COILS] = {false};  // 각 코일 올라감 상태
int  sel = 0;                  // 벤치 명령 대상 코일

// ---------- 자동 반복 테스트 (t 키) ----------
bool          autoTest     = false;
bool          nextIsPush   = true;
int           intervalMs   = 3000;  // 펄스 후 전기 0 대기 — 무전원 래치 유지 확인용으로 길게
unsigned long lastFireAt   = 0;

// ---------- 켜고-끄고 교대 모드 (r 키) ----------
bool          altMode      = false;
bool          altOn        = false;
unsigned long altStartAt   = 0;
const unsigned long ALT_MAX_MS = 60000;

// ---------- B: 패턴 라인 수신 버퍼 ----------
char          lineBuf[16];
int           lineLen      = 0;
bool          capturing    = false;
unsigned long captureAt    = 0;

// ---- IN2를 LEDC에서 떼고 일반 GPIO로 되돌려 레벨 출력 ----
// (ledcDetachPin 후에는 pinMode를 다시 잡아야 확실히 GPIO로 돌아온다)
void in2Gpio(int i, int level) {
  ledcDetachPin(COILS[i][1]);
  pinMode(COILS[i][1], OUTPUT);
  digitalWrite(COILS[i][1], level);
}

// 슬로 디케이 홀드: IN1=HIGH 고정, IN2를 공유 채널 PWM에 attach.
// (IN2가 LOW인 동안 구동, HIGH인 동안 브레이크 — 전류가 안 끊기고 순환.
//  듀티 255-holdDuty로 쓰면 실효 구동률이 holdDuty/255가 된다)
void in2Hold(int i) {
  ledcAttachPin(COILS[i][1], CH_HOLD);
}

// 원시 전원 차단 — 래치 모드에선 핀이 매달린 채 남을 수 있음 (동기화는 d)
void coilOff(int i) {
  digitalWrite(COILS[i][0], LOW);
  in2Gpio(i, LOW);
  if (!latchMode) up[i] = false;  // 홀드 모드에선 전원 차단 = 낙하
}

// 밀기 펄스 후 전원 0 — 자석이 임계점을 넘으면 무전원으로 매달림 (래치)
void coilPushPulse(int i) {
  digitalWrite(COILS[i][0], HIGH);
  in2Gpio(i, LOW);            // 풀 구동
  delay(pulseMs);
  digitalWrite(COILS[i][0], LOW);
  up[i] = latchMode;          // 래치 모드에서만 '올라감'으로 기록
}

// 당기기 펄스 (반대 극성) — 래치된 자석을 끌어내려 복귀
void coilPullPulse(int i) {
  digitalWrite(COILS[i][0], LOW);
  in2Gpio(i, HIGH);
  delay(pulseMs);
  in2Gpio(i, LOW);
  up[i] = false;
}

// 올리기 — 래치: 펄스만 / 홀드: 팝 후 홀드 PWM 전환
void coilRaise(int i) {
  if (latchMode) { coilPushPulse(i); return; }
  digitalWrite(COILS[i][0], HIGH);
  in2Gpio(i, LOW);
  delay(pulseMs);
  in2Hold(i);
  up[i] = true;
}

// 내리기 — 래치: 당김 펄스로 강제 복귀 / 홀드: 전원 차단으로 자연 복귀
void coilRelease(int i) {
  if (latchMode) { coilPullPulse(i); return; }
  coilOff(i);
}

void allOff() {
  for (int i = 0; i < NUM_COILS; i++) coilRelease(i);
}

int countUp() {
  int n = 0;
  for (int i = 0; i < NUM_COILS; i++) if (up[i]) n++;
  return n;
}

String patternStr() {
  String s;
  for (int i = 0; i < NUM_COILS; i++) s += up[i] ? '1' : '0';
  return s;
}

// ---- B:XXXXXXXXX 패턴 적용 ----
// '0' 자리는 반대 극성(당김) 펄스를 무조건 발사, '1' 자리도 무조건 재푸시.
// up[] 추적에 의존하지 않아 크로스토크·탈조로 어긋난 핀도 매번 물리적으로
// 패턴과 일치하게 정렬된다. 전부 순차 발사 (동시 통전 금지 — 전원 딥 방지)
void applyPattern(const char* p) {
  bool first = true;
  for (int i = 0; i < NUM_COILS; i++) {
    if (p[i] == '1') continue;
    if (!first) delay(popGapMs);
    coilRelease(i);
    first = false;
  }
  for (int i = 0; i < NUM_COILS; i++) {
    if (p[i] == '0') continue;
    if (!first) delay(popGapMs);
    coilRaise(i);
    first = false;
  }
  if (latchMode) {
    Serial.printf("[OK] %s up=%d (latch, 유지전류 0)\n", patternStr().c_str(), countUp());
  } else {
    // 홀드 전류 어림: 풀온 ~1.1A 기준 × 실효 듀티 × 개수 (전원 여유 확인용)
    float amp = countUp() * 1.1f * holdDuty / 255.0f;
    Serial.printf("[OK] %s up=%d (~%.1fA hold)\n", patternStr().c_str(), countUp(), amp);
    if (amp > 1.0f)
      Serial.println("[WARN] 홀드 전류가 1A 초과 추정 - 벤치 서플라이 한도(1.2A) 확인!");
  }
}

void parseLine() {
  lineBuf[lineLen] = '\0';
  bool ok = (lineLen == 11) && lineBuf[0] == 'B' && lineBuf[1] == ':';
  for (int i = 2; ok && i < 11; i++)
    if (lineBuf[i] != '0' && lineBuf[i] != '1') ok = false;
  if (ok) applyPattern(lineBuf + 2);
  else    Serial.printf("[ERR] bad pattern '%s' (need B:XXXXXXXXX, 9x 0/1)\n", lineBuf);
}

void printStatus() {
  Serial.printf("[STATUS] mode=%s pat=%s sel=%d pulse=%dms hold=%d/255 (%.0f%%) gap=%dms interval=%dms auto=%s alt=%s\n",
                latchMode ? "LATCH" : "HOLD",
                patternStr().c_str(), sel, pulseMs, holdDuty, holdDuty * 100.0 / 255,
                popGapMs, intervalMs, autoTest ? "ON" : "OFF", altMode ? "ON" : "OFF");
}

void printHelp() {
  Serial.println("---- braille 9-coil ----");
  Serial.println(" B:XXXXXXXXX(엔터) : 9자리 패턴 적용 (Pi용, 행 우선)");
  Serial.println(" 0~8 : 튜닝 대상 코일 선택");
  Serial.println(" u : 선택 코일 올리기   d : 전부 내리기(당김 스윕)+모드 정지");
  Serial.println(" h : 래치/홀드 모드 전환 (기본 래치 - 펄스 후 전원 0)");
  Serial.println(" p : 밀기 펄스만   q : 당기기 펄스   o : 100% 연속 ON (발열주의)");
  Serial.println(" t : 자동 반복 펄스   r : 켜고-끄고 교대 (60초 자동정지)");
  Serial.println(" a : 전 코일 스윕 (0→8 PUSH → 대기 → 전부 PULL)");
  Serial.println(" +/- : 펄스폭   [/] : 홀드듀티   </> : 반복간격");
  Serial.println(" s : 상태   ? : 도움말");
}

void setup() {
  for (int i = 0; i < NUM_COILS; i++) {
    pinMode(COILS[i][0], OUTPUT);
    digitalWrite(COILS[i][0], LOW);
    pinMode(COILS[i][1], OUTPUT);
    digitalWrite(COILS[i][1], LOW);
  }
  // Arduino core 2.x LEDC API. 채널 하나만 쓰고 전 코일이 공유한다.
  ledcSetup(CH_HOLD, PWM_FREQ, PWM_RES);
  ledcWrite(CH_HOLD, 255 - holdDuty);

  // 리부트 전 래치로 매달려 있던 핀을 전부 당겨 내림 — 물리 상태 동기화
  for (int i = 0; i < NUM_COILS; i++) coilPullPulse(i);

  Serial.begin(115200);
  delay(500);
  printHelp();
  printStatus();

  // 벤치: 지금은 코일0(GPIO4/5)만 연결됨 — 부팅하면 바로 래치 테스트 자동 시작.
  // (PUSH 후 전기 0 대기 → PULL 교대. t로 정지, d로 전체 OFF)
  autoTest = true;
  nextIsPush = true;
  lastFireAt = millis() - intervalMs;  // 켜자마자 첫 발사
  Serial.printf("[AUTO] 부팅 자동시작: coil %d - 펄스 %dms, 간격 %dms (t로 정지)\n", sel, pulseMs, intervalMs);
}

void loop() {
  // ---- 자동 반복 테스트 (선택 코일, 논블로킹) ----
  if (autoTest && millis() - lastFireAt >= (unsigned long)intervalMs) {
    if (nextIsPush) { coilPushPulse(sel); Serial.printf("[PUSH] %dms\n", pulseMs); }
    else            { coilPullPulse(sel); Serial.printf("[PULL] %dms\n", pulseMs); }
    nextIsPush = !nextIsPush;
    lastFireAt = millis();
  }

  // ---- 켜고-끄고 교대 모드 (선택 코일) ----
  if (altMode) {
    if (millis() - altStartAt >= ALT_MAX_MS) {
      altMode = false;
      coilOff(sel);
      Serial.println("[ALT] 60초 자동 정지 (발열 보호) - r로 재시작");
    } else if (millis() - lastFireAt >= (unsigned long)intervalMs) {
      if (altOn) {
        coilOff(sel);
        altOn = false;
        Serial.println("[ALT] OFF");
      } else {
        nextIsPush = !nextIsPush;
        if (nextIsPush) { digitalWrite(COILS[sel][0], HIGH); in2Gpio(sel, LOW); }
        else            { digitalWrite(COILS[sel][0], LOW);  in2Gpio(sel, HIGH); }
        altOn = true;
        Serial.println(nextIsPush ? "[ALT] ON (PUSH)" : "[ALT] ON (PULL)");
      }
      lastFireAt = millis();
    }
  }

  // ---- B: 라인 캡처 타임아웃 ----
  if (capturing && millis() - captureAt > 500) {
    capturing = false;
    lineLen = 0;
    Serial.println("[ERR] pattern timeout");
  }

  if (!Serial.available()) return;
  char c = Serial.read();

  // ---- B로 시작하는 줄은 개행까지 모아서 패턴 파싱 ----
  if (capturing) {
    if (c == '\n' || c == '\r') { parseLine(); capturing = false; lineLen = 0; }
    else if (lineLen < (int)sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
    else { capturing = false; lineLen = 0; Serial.println("[ERR] pattern too long"); }
    return;
  }
  if (c == 'B') {
    capturing = true;
    captureAt = millis();
    lineLen = 0;
    lineBuf[lineLen++] = c;
    return;
  }

  if (c >= '0' && c <= '8') {
    sel = c - '0';
    Serial.printf("[SEL] coil %d (IN1=GPIO%d, IN2=GPIO%d)\n", sel, COILS[sel][0], COILS[sel][1]);
    return;
  }

  switch (c) {
    case 'u': coilRaise(sel);
              if (latchMode) Serial.printf("[RAISE] coil %d 래치 펄스 %dms (유지전류 0)\n", sel, pulseMs);
              else           Serial.printf("[RAISE] coil %d pop %dms -> hold %d/255\n", sel, pulseMs, holdDuty);
              break;
    case 'o': digitalWrite(COILS[sel][0], HIGH); in2Gpio(sel, LOW); up[sel] = true;
              Serial.println("[FULL ON] 100% 연속 - 약 1.1A. 10초 안에 d로 끌 것(발열)");
              break;
    case 'd': allOff();
              autoTest = false; altMode = false;
              Serial.println("[RELEASE] 전부 OFF - 자석 복귀");
              break;
    case 'p': coilPushPulse(sel);
              Serial.printf("[PUSH] coil %d %dms\n", sel, pulseMs);
              break;
    case 'q': coilPullPulse(sel);
              Serial.printf("[PULL] coil %d %dms\n", sel, pulseMs);
              break;
    case 'r':
      altMode = !altMode;
      if (altMode) {
        autoTest = false;
        nextIsPush = true;
        altOn = true;
        digitalWrite(COILS[sel][0], HIGH); in2Gpio(sel, LOW);
        altStartAt = lastFireAt = millis();
        Serial.printf("[ALT] coil %d - %dms ON / OFF 교대, 60초 자동정지 (r로 정지)\n", sel, intervalMs);
        Serial.println("[ALT] ON (PUSH)");
      } else {
        coilOff(sel);
        Serial.println("[ALT] 정지 - 코일 OFF");
      }
      break;
    case 't':
      autoTest = !autoTest;
      if (autoTest) {
        altMode = false;
        nextIsPush = true;
        lastFireAt = millis() - intervalMs;  // 켜자마자 첫 발사
        Serial.printf("[AUTO] coil %d - 펄스 %dms, 간격 %dms (t로 정지)\n", sel, pulseMs, intervalMs);
      } else {
        coilOff(sel);
        Serial.println("[AUTO] 정지 - 코일 OFF");
      }
      break;
    case '>': intervalMs += 250; printStatus(); break;
    case '<': intervalMs = max(250, intervalMs - 250); printStatus(); break;
    case '+': pulseMs += 10; printStatus(); break;
    case '-': pulseMs = max(10, pulseMs - 10); printStatus(); break;
    case ']': holdDuty = min(255, holdDuty + 10);
              ledcWrite(CH_HOLD, 255 - holdDuty);  // 홀드 중인 전 코일에 즉시 반영
              printStatus(); break;
    case '[': holdDuty = max(0, holdDuty - 10);
              ledcWrite(CH_HOLD, 255 - holdDuty);
              printStatus(); break;
    case 'a': {  // 전 코일 스윕: 순차 PUSH → 무전원 래치 관찰 → 순차 PULL
      autoTest = false; altMode = false;
      Serial.printf("[SWEEP] 0→8 순차 PUSH (펄스 %dms, 간격 %dms)\n", pulseMs, popGapMs);
      for (int i = 0; i < NUM_COILS; i++) {
        coilPushPulse(i);
        Serial.printf("[SWEEP] coil %d PUSH\n", i);
        if (i < NUM_COILS - 1) delay(popGapMs);
      }
      printStatus();
      Serial.printf("[SWEEP] %dms 무전원 래치 관찰 후 전부 내림...\n", intervalMs);
      delay(intervalMs);
      for (int i = 0; i < NUM_COILS; i++) {
        coilPullPulse(i);
        Serial.printf("[SWEEP] coil %d PULL\n", i);
        if (i < NUM_COILS - 1) delay(popGapMs);
      }
      printStatus();
      Serial.println("[SWEEP] 완료 - 안 올라간/안 내려간 셀 번호를 기록할 것");
      break;
    }
    case 'h': latchMode = !latchMode;
              Serial.printf("[MODE] %s\n", latchMode ? "LATCH - 펄스 후 전원 0 유지" : "HOLD - 팝 후 PWM 유지");
              break;
    case 's': printStatus(); break;
    case '?': printHelp();   break;
    default: break;  // 엔터/공백 등은 무시
  }
}

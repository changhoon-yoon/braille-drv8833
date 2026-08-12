// ============================================================
// 점자 코일 벤치 테스트 펌웨어 (ESP32-S3 N16R8 + DRV8833)
//
// [배선]
//   DRV8833 VM      <- 외부 5V (모듈 가까이에 470uF, 긴다리=+가 5V)
//   DRV8833 GND     <- 전원 GND  ── ESP32 GND 와 반드시 공통!
//   DRV8833 EN/SLP  <- ESP32 3V3 (이거 빼먹으면 전부 침묵)
//   DRV8833 AIN1    <- GPIO 4
//   DRV8833 AIN2    <- GPIO 5
//   DRV8833 AOUT1/2 <- 코일 양 끝 (방향 무관)
//
// [ESP32-S3 N16R8 핀 주의]
//   GPIO 26~37 = 플래시/PSRAM 전용 → 절대 사용 금지
//   GPIO 0, 19, 20, 43, 44, 45, 46 = 부트/USB/UART → 회피
//   안전한 핀: 4~18, 21, 38~42, 47, 48
//
// [시리얼 명령 - 115200bps]
//   u : 올리기 (팝 100% -> 홀드 PWM 유지)
//   d : 내리기 (코일 OFF, 자석 복귀)
//   p : 밀기 펄스만 (홀드 없음)
//   q : 당기기 펄스 (반대 방향, 래칭 실험용)
//   + / - : 펄스폭 10ms 증감
//   [ / ] : 홀드 듀티 10 증감 (0~255)
//   s : 현재 설정 출력
//   ? : 도움말
// ============================================================

#include <Arduino.h>

// ---------- 핀/채널 설정 (확장 시 이 표에 추가) ----------
const int PIN_AIN1 = 4;   // 밀기 방향 입력
const int PIN_AIN2 = 5;   // 당기기 방향 입력

const int CH_PUSH = 0;    // LEDC 채널 (S3는 0~7 사용 가능)
const int CH_PULL = 1;

const int PWM_FREQ = 20000; // 20kHz: 사람 귀에 안 들림
const int PWM_RES  = 8;     // 듀티 0~255

// ---------- 튜닝 변수 (시리얼로 실시간 조정) ----------
int  pulseMs  = 40;   // 팝/펄스 폭. 실험으로 최소값 찾기 (40 -> 30 -> 20...)
int  holdDuty = 70;   // 홀드 듀티. 70/255 = 약 27%. 핀이 안 떨어지는 최소값 찾기
bool holding  = false;

// ---------- 자동 반복 테스트 (t 키) ----------
// 밀기 펄스 -> OFF -> intervalMs 대기 -> 당기기 펄스 -> OFF -> 대기 -> 반복
bool          autoTest     = false;
bool          nextIsPush   = true;
int           intervalMs   = 1000;
unsigned long lastFireAt   = 0;

// ---------- 켜고-끄고 교대 모드 (r 키) ----------
// [방향A ON intervalMs] -> [OFF intervalMs] -> [방향B ON] -> [OFF] -> 반복
// 듀티 50%라 발열 절반, 60초 후 자동 정지
bool          altMode      = false;
bool          altOn        = false;
unsigned long altStartAt   = 0;
const unsigned long ALT_MAX_MS = 60000;

// 반대쪽을 먼저 끄고 나서 켠다 → 둘 다 HIGH(브레이크) 원천 차단
void coilWrite(int chOn, int chOff, int duty) {
  ledcWrite(chOff, 0);
  ledcWrite(chOn, duty);
}

void coilOff() {
  ledcWrite(CH_PUSH, 0);
  ledcWrite(CH_PULL, 0);
}

// u: 팝(100%) 후 홀드 듀티로 유지 — 단방향(ON=밀기) 방식
// 홀드는 "슬로 디케이" PWM: AIN1=HIGH 고정, AIN2를 PWM.
// (AIN2가 LOW인 동안 구동, HIGH인 동안 브레이크 — 코일 전류가 안 끊기고
//  순환해서 평균 전류가 듀티에 비례함. AIN1만 PWM하면(fast decay)
//  전류가 매 주기 무너져 평균이 수십 mA로 붕괴함)
void raiseAndHold() {
  coilWrite(CH_PUSH, CH_PULL, 255);
  delay(pulseMs);
  ledcWrite(CH_PUSH, 255);
  ledcWrite(CH_PULL, 255 - holdDuty);
  holding = true;
  Serial.printf("[RAISE] pop %dms -> hold %d/255 (slow decay)\n", pulseMs, holdDuty);
}

// o: 100% 연속 ON — 전류계로 풀로드(~1.1A) 확인용. 발열 주의!
void fullOn() {
  coilWrite(CH_PUSH, CH_PULL, 255);
  holding = true;
  Serial.println("[FULL ON] 100% 연속 - 약 1.1A. 10초 안에 d로 끌 것(발열)");
}

// d: 전원 차단 — 자석이 못 코어로 스스로 복귀 (모든 자동 모드도 정지)
void release() {
  coilOff();
  holding = false;
  autoTest = false;
  altMode = false;
  Serial.println("[RELEASE] OFF - 자석 복귀");
}

// p: 밀기 펄스만 (홀드 없음, 힘 관찰용)
void pushPulse() {
  coilWrite(CH_PUSH, CH_PULL, 255);
  delay(pulseMs);
  coilOff();
  holding = false;
  Serial.printf("[PUSH] %dms\n", pulseMs);
}

// q: 당기기 펄스 (반대 극성 — 래칭 방식 실험용)
void pullPulse() {
  coilWrite(CH_PULL, CH_PUSH, 255);
  delay(pulseMs);
  coilOff();
  holding = false;
  Serial.printf("[PULL] %dms\n", pulseMs);
}

void printStatus() {
  Serial.printf("[STATUS] pulse=%dms hold=%d/255 (%.0f%%) interval=%dms auto=%s state=%s\n",
                pulseMs, holdDuty, holdDuty * 100.0 / 255, intervalMs,
                autoTest ? "ON" : "OFF",
                holding ? "HOLDING" : "OFF");
}

void printHelp() {
  Serial.println("---- braille coil bench ----");
  Serial.println(" u : 올리기 (팝->홀드)");
  Serial.println(" o : 100% 연속 ON (전류계 확인용, 발열주의)");
  Serial.println(" d : 내리기 (OFF)");
  Serial.println(" p : 밀기 펄스만");
  Serial.println(" q : 당기기 펄스");
  Serial.println(" t : 자동 반복 펄스 (밀기->1초->당기기->1초...)");
  Serial.println(" r : 켜고-끄고 교대 (1초 ON -> 1초 OFF, 방향 번갈아, 60초 자동정지)");
  Serial.println(" < / > : 반복 간격 -+250ms");
  Serial.println(" +/- : 펄스폭 +-10ms");
  Serial.println(" [/] : 홀드듀티 -+10");
  Serial.println(" s : 상태  ? : 도움말");
}

void setup() {
  // Arduino core 2.x LEDC API (PlatformIO 기본 플랫폼 기준)
  ledcSetup(CH_PUSH, PWM_FREQ, PWM_RES);
  ledcSetup(CH_PULL, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_AIN1, CH_PUSH);
  ledcAttachPin(PIN_AIN2, CH_PULL);
  coilOff();  // 부팅 직후 확실히 꺼진 상태로 시작

  Serial.begin(115200);
  delay(500);
  printHelp();
  printStatus();
}

void loop() {
  // ---- 자동 반복 테스트 (논블로킹: 대기 중에도 키 입력 받음) ----
  if (autoTest && millis() - lastFireAt >= (unsigned long)intervalMs) {
    if (nextIsPush) pushPulse();
    else            pullPulse();
    nextIsPush = !nextIsPush;
    lastFireAt = millis();
  }

  // ---- 켜고-끄고 교대 모드 ----
  if (altMode) {
    if (millis() - altStartAt >= ALT_MAX_MS) {
      altMode = false;
      coilOff();
      Serial.println("[ALT] 60초 자동 정지 (발열 보호) - r로 재시작");
    } else if (millis() - lastFireAt >= (unsigned long)intervalMs) {
      if (altOn) {
        coilOff();
        altOn = false;
        Serial.println("[ALT] OFF");
      } else {
        nextIsPush = !nextIsPush;
        if (nextIsPush) coilWrite(CH_PUSH, CH_PULL, 255);
        else            coilWrite(CH_PULL, CH_PUSH, 255);
        altOn = true;
        Serial.println(nextIsPush ? "[ALT] ON (PUSH)" : "[ALT] ON (PULL)");
      }
      lastFireAt = millis();
    }
  }

  if (!Serial.available()) return;
  char c = Serial.read();

  switch (c) {
    case 'u': raiseAndHold(); break;
    case 'o': fullOn();       break;
    case 'd': release();      break;
    case 'p': pushPulse();    break;
    case 'q': pullPulse();    break;
    case 'r':
      altMode = !altMode;
      if (altMode) {
        autoTest = false;
        nextIsPush = true;
        altOn = true;
        coilWrite(CH_PUSH, CH_PULL, 255);
        altStartAt = lastFireAt = millis();
        Serial.printf("[ALT] 시작 - %dms ON / %dms OFF 교대, 60초 자동정지 (r로 정지)\n", intervalMs, intervalMs);
        Serial.println("[ALT] ON (PUSH)");
      } else {
        coilOff(); holding = false;
        Serial.println("[ALT] 정지 - 코일 OFF");
      }
      break;
    case 't':
      autoTest = !autoTest;
      if (autoTest) {
        altMode = false;
        nextIsPush = true;
        lastFireAt = millis() - intervalMs;  // 켜자마자 첫 발사
        Serial.printf("[AUTO] 시작 - 펄스 %dms, 간격 %dms (t로 정지)\n", pulseMs, intervalMs);
      } else {
        coilOff(); holding = false;
        Serial.println("[AUTO] 정지 - 코일 OFF");
      }
      break;
    case '>': intervalMs += 250; printStatus(); break;
    case '<': intervalMs = max(250, intervalMs - 250); printStatus(); break;
    case '+': pulseMs += 10; printStatus(); break;
    case '-': pulseMs = max(10, pulseMs - 10); printStatus(); break;
    case ']': holdDuty = min(255, holdDuty + 10);
              if (holding) ledcWrite(CH_PULL, 255 - holdDuty);
              printStatus(); break;
    case '[': holdDuty = max(0, holdDuty - 10);
              if (holding) ledcWrite(CH_PULL, 255 - holdDuty);
              printStatus(); break;
    case 's': printStatus(); break;
    case '?': printHelp();   break;
    default: break;  // 엔터/공백 등은 무시
  }
}

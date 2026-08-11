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
void raiseAndHold() {
  coilWrite(CH_PUSH, CH_PULL, 255);
  delay(pulseMs);
  ledcWrite(CH_PUSH, holdDuty);
  holding = true;
  Serial.printf("[RAISE] pop %dms -> hold %d/255\n", pulseMs, holdDuty);
}

// d: 전원 차단 — 자석이 못 코어로 스스로 복귀
void release() {
  coilOff();
  holding = false;
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
  Serial.printf("[STATUS] pulse=%dms hold=%d/255 (%.0f%%) state=%s\n",
                pulseMs, holdDuty, holdDuty * 100.0 / 255,
                holding ? "HOLDING" : "OFF");
}

void printHelp() {
  Serial.println("---- braille coil bench ----");
  Serial.println(" u : 올리기 (팝->홀드)");
  Serial.println(" d : 내리기 (OFF)");
  Serial.println(" p : 밀기 펄스만");
  Serial.println(" q : 당기기 펄스");
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
  if (!Serial.available()) return;
  char c = Serial.read();

  switch (c) {
    case 'u': raiseAndHold(); break;
    case 'd': release();      break;
    case 'p': pushPulse();    break;
    case 'q': pullPulse();    break;
    case '+': pulseMs += 10; printStatus(); break;
    case '-': pulseMs = max(10, pulseMs - 10); printStatus(); break;
    case ']': holdDuty = min(255, holdDuty + 10);
              if (holding) ledcWrite(CH_PUSH, holdDuty);
              printStatus(); break;
    case '[': holdDuty = max(0, holdDuty - 10);
              if (holding) ledcWrite(CH_PUSH, holdDuty);
              printStatus(); break;
    case 's': printStatus(); break;
    case '?': printHelp();   break;
    default: break;  // 엔터/공백 등은 무시
  }
}

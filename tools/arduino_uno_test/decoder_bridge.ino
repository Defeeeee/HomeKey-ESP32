/*
  ===========================================================================
  ARDUINO UNO: RF SENSOR DECODER & ESP32 BRIDGE (DSC WS4945 + Gadnic WS1000)
  RF Data In -> Uno Pin 2 (hardware interrupt)
  Bridge Out -> Uno Pins 3 / 4 / 5 (one per sensor, through voltage dividers)

  The ISR only captures raw pulse timing and shifts bits into two buffers
  (DSC continuous, WS1000 sync-framed) — no matching, no digitalWrite, no
  function calls happen inside the interrupt. All decoding/matching moved to
  loop(), so the interrupt handler stays as short as possible and can't miss
  a subsequent RF edge while it's still busy comparing codes.
  ===========================================================================
*/

// --- SIGNAL TIMINGS (shared receiver; re-tune if WS1000 doesn't decode cleanly) ---
#define BIT_MIN       150
#define BIT_MAX       2500
#define DATA_PIVOT    550

// Any gap longer than this is treated as the sync/preamble gap between repeats.
// Tune upward if WS1000 codes never get confirmed (print raw durations to check).
#define SYNC_MIN      3000

// Same word must arrive this many times in a row (across sync-delimited frames)
// before it's trusted as a real WS1000 code, to reject RF noise.
#define CONFIRM_REPEATS 3

// Set to 1 to print every CONFIRMED WS1000 word seen (used to learn new sensor codes).
// Leave at 0 for normal operation once all codes are known, to avoid Serial spam.
#define SNIFF_MODE 0

const int DSC_OUT_PIN      = 3; // Zone 1 (existing, unchanged)
const int WS1000_A_OUT_PIN = 4; // WS1000 sensor #1 -> new zone
const int WS1000_B_OUT_PIN = 5; // WS1000 sensor #2 -> new zone
const unsigned long COOLDOWN_TIME = 1000;

// --- Legacy DSC accumulator (ISR only shifts bits; loop() does the matching) ---
volatile unsigned long lastPulseTime = 0;
volatile uint32_t rollingBuffer = 0;
volatile uint32_t dscReadyWord = 0;
volatile bool dscWordReady = false;

// --- Sync+repeat accumulator for WS1000 (ISR only shifts bits; loop() confirms) ---
volatile uint32_t syncWord = 0;
volatile uint8_t syncBitCount = 0;
volatile uint32_t syncReadyWord = 0;
volatile uint8_t syncReadyBits = 0;
volatile bool syncWordReady = false;

uint32_t candidateWord = 0;
uint8_t candidateBits = 0;
uint8_t candidateRepeats = 0;

volatile byte dscStateFlag = 0;    // 0 = no change, 1 = open, 2 = closed
volatile byte ws1000AStateFlag = 0;
volatile byte ws1000BStateFlag = 0;

unsigned long lastPrintTime = 0;

// --- Known DSC WS4945 codes (unchanged) ---
const uint32_t DSC_OPEN_CODES[]   = { 0x028A2A2, 0xC00A28A8, 0x80145151, 0x80155151, 0x54544551 };
const uint32_t DSC_CLOSED_CODES[] = { 0x02828A8, 0x80141454, 0x0505151, 0x02828A88 };

// --- Gadnic WS1000 codes (captured via SNIFF_MODE, 49-bit sync-delimited words) ---
const uint32_t WS1000_A_OPEN_CODES[]   = { 0xD4D4D4B4 };
const uint32_t WS1000_A_CLOSED_CODES[] = { 0xD4D4D52C };
const uint32_t WS1000_B_OPEN_CODES[]   = { 0x4D4CD4B4 };
const uint32_t WS1000_B_CLOSED_CODES[] = { 0x4D4CD52C };

bool matches(uint32_t value, const uint32_t* codes, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (value == codes[i]) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(DSC_OUT_PIN, OUTPUT);
  pinMode(WS1000_A_OUT_PIN, OUTPUT);
  pinMode(WS1000_B_OUT_PIN, OUTPUT);
  digitalWrite(DSC_OUT_PIN, LOW);
  digitalWrite(WS1000_A_OUT_PIN, LOW);
  digitalWrite(WS1000_B_OUT_PIN, LOW);

  pinMode(2, INPUT);
  attachInterrupt(0, rfDecoderISR, CHANGE);

  Serial.println(F("\n=================================================="));
  Serial.println(F("   RF DECODER & BRIDGE READY (DSC + WS1000 x2)     "));
#if SNIFF_MODE
  Serial.println(F("   SNIFF_MODE = 1 -> printing confirmed WS1000 words"));
#endif
  Serial.println(F("=================================================="));
}

void loop() {
  if (dscWordReady) {
    noInterrupts();
    uint32_t word = dscReadyWord;
    dscWordReady = false;
    interrupts();

    if (matches(word, DSC_OPEN_CODES, sizeof(DSC_OPEN_CODES) / sizeof(DSC_OPEN_CODES[0]))) {
      digitalWrite(DSC_OUT_PIN, HIGH);
      dscStateFlag = 1;
    } else if (matches(word, DSC_CLOSED_CODES, sizeof(DSC_CLOSED_CODES) / sizeof(DSC_CLOSED_CODES[0]))) {
      digitalWrite(DSC_OUT_PIN, LOW);
      dscStateFlag = 2;
    }
  }

  if (syncWordReady) {
    noInterrupts();
    uint32_t word = syncReadyWord;
    uint8_t bits = syncReadyBits;
    syncWordReady = false;
    interrupts();

    if (bits == candidateBits && word == candidateWord) {
      if (candidateRepeats < 255) candidateRepeats++;
    } else {
      candidateWord = word;
      candidateBits = bits;
      candidateRepeats = 1;
    }

    if (candidateRepeats == CONFIRM_REPEATS) {
#if SNIFF_MODE
      Serial.print(F("[SNIFF] confirmed word (0x"));
      Serial.print(candidateWord, HEX);
      Serial.print(F(", "));
      Serial.print(candidateBits);
      Serial.println(F(" bits)"));
#endif
      driveWs1000Outputs(candidateWord);
    }
  }

  if (dscStateFlag != 0) {
    byte s = dscStateFlag;
    dscStateFlag = 0;
    printZoneState(F("DSC"), s);
  }
  if (ws1000AStateFlag != 0) {
    byte s = ws1000AStateFlag;
    ws1000AStateFlag = 0;
    printZoneState(F("WS1000-A"), s);
  }
  if (ws1000BStateFlag != 0) {
    byte s = ws1000BStateFlag;
    ws1000BStateFlag = 0;
    printZoneState(F("WS1000-B"), s);
  }
}

void printZoneState(const __FlashStringHelper* label, byte state) {
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime < COOLDOWN_TIME) return;
  lastPrintTime = currentTime;

  Serial.print(F("[BRIDGE] "));
  Serial.print(label);
  Serial.println(state == 1 ? F(" ALARM TRACKED: Sending HIGH") : F(" SYSTEM NORMAL: Sending LOW"));
}

void driveWs1000Outputs(uint32_t code) {
  if (matches(code, WS1000_A_OPEN_CODES, sizeof(WS1000_A_OPEN_CODES) / sizeof(uint32_t))) {
    digitalWrite(WS1000_A_OUT_PIN, HIGH);
    ws1000AStateFlag = 1;
  } else if (matches(code, WS1000_A_CLOSED_CODES, sizeof(WS1000_A_CLOSED_CODES) / sizeof(uint32_t))) {
    digitalWrite(WS1000_A_OUT_PIN, LOW);
    ws1000AStateFlag = 2;
  }

  if (matches(code, WS1000_B_OPEN_CODES, sizeof(WS1000_B_OPEN_CODES) / sizeof(uint32_t))) {
    digitalWrite(WS1000_B_OUT_PIN, HIGH);
    ws1000BStateFlag = 1;
  } else if (matches(code, WS1000_B_CLOSED_CODES, sizeof(WS1000_B_CLOSED_CODES) / sizeof(uint32_t))) {
    digitalWrite(WS1000_B_OUT_PIN, LOW);
    ws1000BStateFlag = 2;
  }
}

void rfDecoderISR() {
  unsigned long now = micros();
  unsigned long duration = now - lastPulseTime;
  lastPulseTime = now;

  if (duration >= BIT_MIN && duration <= BIT_MAX) {
    bool bit = (duration > DATA_PIVOT);

    rollingBuffer = (rollingBuffer << 1) | bit;
    dscReadyWord = rollingBuffer;
    dscWordReady = true;

    syncWord = (syncWord << 1) | bit;
    syncBitCount++;
    return;
  }

  if (duration > SYNC_MIN && syncBitCount > 0) {
    syncReadyWord = syncWord;
    syncReadyBits = syncBitCount;
    syncWordReady = true;
  }
  syncWord = 0;
  syncBitCount = 0;
}

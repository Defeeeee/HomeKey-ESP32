/*
  ===========================================================================
  ARDUINO UNO: RF SENSOR DECODER & ESP32 BRIDGE (DSC WS4945 + Gadnic WS1000)
  RF Data In -> Uno Pin 2 (hardware interrupt)
  Bridge Out -> Uno Pins 3 / 4 / 5 (one per sensor, through voltage dividers)

  Every sensor is now decoded the same way: frame on the sync gap, then match
  the complete sync-delimited word against known codes. The DSC used to be
  decoded by a CONTINUOUS, UNFRAMED rolling-buffer match, which is what caused
  the long-standing "Zone 5 stuck open" fault:

      old: a fresh 32-bit window was tested on EVERY pulse edge (~1000/s)
           against 9 codes -> ~7.8e8 comparisons/day against a 2^32 space
           -> a coincidental "open" match roughly every few days, which then
           latched the output HIGH forever (nothing ever reset it).

      now: only complete framed words are tested (a few hundred per day)
           against 2 exact codes -> a false match is effectively impossible.

  Captured 2026-07-26 with the sniff build. Note the DSC repeats a burst ~4x,
  but reception quality varies: 2 of 5 observed events yielded only ONE
  decodable frame. Requiring several repeats would therefore MISS real
  openings, so a single well-framed exact match is acted on immediately —
  the framing itself is the noise defence, not the repeat count.

  Also note the bit count wobbles across repeats of the same transmission
  (48/38/36 observed for one event) because a missed edge shortens the frame.
  The 32-bit word value stays stable, so repeats are counted on the WORD ONLY;
  comparing bit counts as well would reset the counter and break confirmation.

  The ISR only measures pulse timing and shifts bits — no matching, no
  digitalWrite, no function calls — so it can never stay busy long enough to
  miss the next RF edge (this caused an earlier DSC regression).
  ===========================================================================
*/

// --- SIGNAL TIMINGS (shared receiver) ---
#define BIT_MIN       150
#define BIT_MAX       2500
#define DATA_PIVOT    550

// Any gap longer than this delimits one transmission frame.
#define SYNC_MIN      3000

// Ignore frames shorter than this. Real codes are long (WS4945 = 48 bits,
// WS1000 = 49); this is cheap insurance on top of the exact word match.
#define MIN_FRAME_BITS 32

// Repeats required before acting. DSC = 1 because reception is lossy and some
// real events produce only a single decodable frame (see header). WS1000 keeps
// the 3 it was originally validated with.
#define DSC_CONFIRM_REPEATS     1
#define WS1000_CONFIRM_REPEATS  3

// Set to 1 to log framed words when capturing codes from a new sensor.
#define SNIFF_MODE 0

const int DSC_OUT_PIN      = 3; // DSC WS4945 -> ESP32 Zone 5 (GPIO 27)
const int WS1000_A_OUT_PIN = 4; // Gadnic WS1000 #A -> ESP32 Zone 6 (GPIO 15)
const int WS1000_B_OUT_PIN = 5; // Gadnic WS1000 #B -> ESP32 Zone 7 (GPIO 2)
const unsigned long COOLDOWN_TIME = 1000;

// --- Sync-framed accumulator (ISR only shifts bits; loop() matches) ---
volatile unsigned long lastPulseTime = 0;
volatile uint32_t syncWord = 0;
volatile uint8_t syncBitCount = 0;
volatile uint32_t syncReadyWord = 0;
volatile uint8_t syncReadyBits = 0;
volatile bool syncWordReady = false;

uint32_t candidateWord = 0;
uint8_t candidateRepeats = 0;

volatile byte dscStateFlag = 0;    // 0 = no change, 1 = open, 2 = closed
volatile byte ws1000AStateFlag = 0;
volatile byte ws1000BStateFlag = 0;

unsigned long lastPrintTime = 0;

// --- DSC WS4945 (captured 2026-07-26, 48-bit sync-framed words) ---
// Superseded the old unframed rolling-buffer captures, which are kept here only
// as a record of what the broken matcher was looking for:
//   open   { 0x028A2A2, 0xC00A28A8, 0x80145151, 0x80155151, 0x54544551 }
//   closed { 0x02828A8, 0x80141454, 0x0505151, 0x02828A88 }
const uint32_t DSC_OPEN_CODES[]   = { 0xA2A22AA8 };
const uint32_t DSC_CLOSED_CODES[] = { 0x28A88AA8 };

// --- Gadnic WS1000 (sync-framed captures) ---
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
  Serial.println(F("   Todos los sensores con framing por sync.        "));
#if SNIFF_MODE
  Serial.println(F("   SNIFF_MODE = 1 -> registrando palabras          "));
#endif
  Serial.println(F("=================================================="));
}

void loop() {
  if (syncWordReady) {
    noInterrupts();
    uint32_t word = syncReadyWord;
    uint8_t bits = syncReadyBits;
    syncWordReady = false;
    interrupts();

    if (bits >= MIN_FRAME_BITS) { // shorter frames are noise fragments
      // Repeats are counted on the word alone — the bit count is not stable
      // across repeats of one burst, so comparing it too would reset the count.
      if (word == candidateWord) {
        if (candidateRepeats < 255) candidateRepeats++;
      } else {
        candidateWord = word;
        candidateRepeats = 1;
      }

#if SNIFF_MODE
      Serial.print(F("[SNIFF] 0x"));
      Serial.print(word, HEX);
      Serial.print(F("  bits="));
      Serial.print(bits);
      Serial.print(F("  rep="));
      Serial.println(candidateRepeats);
#endif

      if (candidateRepeats >= DSC_CONFIRM_REPEATS) {
        driveDscOutput(word);
      }
      if (candidateRepeats >= WS1000_CONFIRM_REPEATS) {
        driveWs1000Outputs(word);
      }
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

void driveDscOutput(uint32_t code) {
  if (matches(code, DSC_OPEN_CODES, sizeof(DSC_OPEN_CODES) / sizeof(uint32_t))) {
    digitalWrite(DSC_OUT_PIN, HIGH);
    dscStateFlag = 1;
  } else if (matches(code, DSC_CLOSED_CODES, sizeof(DSC_CLOSED_CODES) / sizeof(uint32_t))) {
    digitalWrite(DSC_OUT_PIN, LOW);
    dscStateFlag = 2;
  }
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
    syncWord = (syncWord << 1) | (duration > DATA_PIVOT);
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

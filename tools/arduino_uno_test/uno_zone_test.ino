/**
 * ARDUINO UNO - SECURITY ZONE TESTER
 * Purpose: Test 6 physical zones using PC817 optocouplers.
 * Logic based on your setup: 
 *   - HIGH (Voltage present) = CLOSED (Secure)
 *   - LOW (Ground/No voltage) = OPEN (Breach)
 */

// Define Pins for 6 Zones
const int zonePins[6] = {2, 3, 4, 5, 6, 7};
bool lastStates[6] = {false, false, false, false, false, false};

void setup() {
  Serial.begin(9600);
  Serial.println("--- Starting 6-Zone Hardware Test (Arduino Uno) ---");
  Serial.println("Logic: 1 = CLOSED (Safe), 0 = OPEN (Breach)");
  
  for (int i = 0; i < 6; i++) {
    // Note: Arduino Uno does NOT have internal pull-downs.
    // Ensure your PC817 modules have a pull-down resistor to GND
    // if they don't change to 0 when disconnected.
    pinMode(zonePins[i], INPUT);
  }
}

void loop() {
  bool changed = false;
  
  for (int i = 0; i < 6; i++) {
    int reading = digitalRead(zonePins[i]);
    bool currentState = (reading == HIGH); // True if closed (High voltage)
    
    if (currentState != lastStates[i]) {
      lastStates[i] = currentState;
      Serial.print("⚡ [ZONE ");
      Serial.print(i + 1);
      Serial.print("] changed to: ");
      Serial.println(currentState ? "CLOSED (Safe)" : "OPEN (Breach)");
      changed = true;
    }
  }

  // Periodic status print every 2 seconds if nothing changes
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 2000) {
    Serial.print("Current Status: [ ");
    for (int i = 0; i < 6; i++) {
      Serial.print(digitalRead(zonePins[i]));
      Serial.print(" ");
    }
    Serial.println("]");
    lastUpdate = millis();
  }
  
  delay(50); // Simple debounce and loop relaxation
}

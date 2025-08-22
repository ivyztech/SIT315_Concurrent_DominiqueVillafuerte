#include <Arduino.h>
#include <DHT.h>

//  Pins 
#define PIN_TAMPER     8   
#define PIN_BUTTON     9  

#define LED_GREEN      10  // system ARMED/DISARMED
#define LED_RED        11  // intrusion alert (solid)
#define LED_ORANGE     12  // env warning / heartbeat (blink)

#define PIN_ULTRIG     6
#define PIN_ULECHO     7

#define PIN_MOIST_A    A0
#define PIN_DHT_DATA   2
#define DHTTYPE        DHT22

//  Thresholds 
const long  DIST_CLOSE_CM   = 40;    // alert if object closer than this (when armed)
const float TEMP_HOT_C      = 30.0;  // env warning if >=
const float HUMID_HIGH_PCT  = 50.0;  // env warning if >=
const int   MOIST_DRY_ADC   = 450;   // env warning if >= (depends on sensor)

//  ISR-shared flags 
volatile bool g_timerTick = false;      // 100 ms tick
volatile uint8_t g_pinbSnapshot = 0;    // last PORTB read from ISR
volatile bool g_pcintFired = false;     // pin-change happened
volatile unsigned long g_lastTamperEdgeMs = 0;
volatile unsigned long g_lastBtnEdgeMs    = 0;

//  State 
DHT dht(PIN_DHT_DATA, DHTTYPE);

bool sysArmed        = false;   // toggled by button release
bool tamperEvent     = false;   // set when tamper switch changes to ACTIVE
bool alertActive     = false;   // drives RED LED
bool envWarning      = false;   // drives ORANGE blink faster

float lastTempC      = NAN;
float lastHum        = NAN;
int   lastMoist      = -1;
long  lastDistanceCM = -1;

unsigned long alertClearMs = 0;
uint16_t tickCount = 0;

// Heartbeat / patterns
unsigned long lastBlinkMs = 0;
bool orangeOn = false;

long readUltrasonicCM() {
  pulseIn(PIN_ULECHO, LOW, 30000UL); 

  digitalWrite(PIN_ULTRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRIG, LOW);

  unsigned long dur = pulseIn(PIN_ULECHO, HIGH, 30000UL); // ~> 5 m range
  if (dur == 0) return -1;  // no echo

  long cm = (long)(dur / 58UL);

  if (cm < 2 || cm > 400) return -1;
  return cm;
}

//  PCI Setup
void pciBegin_PORTB_D8_D13() {
  pinMode(PIN_TAMPER, INPUT_PULLUP);   // active LOW when pressed/closed to GND
  pinMode(PIN_BUTTON, INPUT_PULLUP);   // active LOW when pressed

  PCIFR  |= (1 << PCIF0);                  // clear pending
  PCICR  |= (1 << PCIE0);                  // enable PORTB PCINT
  PCMSK0 |= (1 << PCINT0) | (1 << PCINT1); // watch D8 & D9

  g_pinbSnapshot = PINB;                   // initial snapshot
}

//  Timer1 100ms 
void timer1Begin_100ms() {
  noInterrupts();
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0;
  OCR1A = 24999;                           // 100ms @ 16MHz/64
  TCCR1B |= (1 << WGM12);                  // CTC
  TCCR1B |= (1 << CS11) | (1 << CS10);     // /64
  TIMSK1 |= (1 << OCIE1A);
  interrupts();
}

//  Setup 
void setup() {
  Serial.begin(115200);
  while(!Serial) {}

  dht.begin();

  pinMode(PIN_ULTRIG, OUTPUT);
  pinMode(PIN_ULECHO, INPUT);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_ORANGE, OUTPUT);

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_ORANGE, LOW);

  pciBegin_PORTB_D8_D13();
  timer1Begin_100ms();

  Serial.println(F("Smart Room Sentinel (No PIR) ready."));
  Serial.println(F("PCI: D8=Tamper, D9=ArmBtn; RED=Alert; ORANGE=Env/Heartbeat; GREEN=Armed"));
}

//  ISRs 
ISR(PCINT0_vect){ g_pinbSnapshot = PINB; g_pcintFired = true; }
ISR(TIMER1_COMPA_vect){ g_timerTick = true; }

//  Edge handling (decode D8/D9) 
void handlePCINTEdges() {
  static uint8_t prevB = PINB;
  uint8_t nowB = g_pinbSnapshot;
  uint8_t changes = nowB ^ prevB;
  prevB = nowB;

  unsigned long now = millis();

  // TAMPER on D8 (PB0) — active LOW (pressed -> LOW)
  if (changes & (1 << 0)) {
    bool tamperHigh = nowB & (1 << 0); // HIGH=idle, LOW=active
    if (now - g_lastTamperEdgeMs > 80) {
      g_lastTamperEdgeMs = now;
      Serial.print(F("[PCI] TAMPER ")); Serial.println(tamperHigh ? F("RELEASE") : F("ACTIVE"));
      if (!tamperHigh && sysArmed) {    // became ACTIVE (LOW) while armed
        tamperEvent = true;
      }
    }
  }

  // BUTTON on D9 (PB1) — toggle arm on release (rising to HIGH)
  if (changes & (1 << 1)) {
    bool btnHigh = nowB & (1 << 1);
    if (now - g_lastBtnEdgeMs > 120) {
      g_lastBtnEdgeMs = now;
      if (btnHigh) {
        sysArmed = !sysArmed;
        Serial.print(F("[BTN] System ")); Serial.println(sysArmed ? F("ARMED") : F("DISARMED"));
      }
    }
  }
}

//  Periodic scheduler
void runSchedulers() {       // <-- the missing brace!
  unsigned long now = millis();
  tickCount = (tickCount + 1) % 60000;

  // Periodic sensor reads
  if (tickCount % 10 == 0) {                        // every 1s
    long cm = readUltrasonicCM();
    if (cm >= 0) lastDistanceCM = cm;
  }
  if (tickCount % 20 == 0) {                        // every 2s
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int moist = analogRead(PIN_MOIST_A);
    if (!isnan(h)) lastHum = h;
    if (!isnan(t)) lastTempC = t;
    lastMoist = moist;

    // Evaluate env warning
    envWarning = false;
    if (!isnan(lastTempC) && lastTempC >= TEMP_HOT_C)  envWarning = true;
    if (!isnan(lastHum)   && lastHum   >= HUMID_HIGH_PCT) envWarning = true;
    if (lastMoist >= 0    && lastMoist >= MOIST_DRY_ADC)  envWarning = true;

    Serial.print(F("[ENV] T=")); Serial.print(lastTempC,1);
    Serial.print(F("C  H=")); Serial.print(lastHum,1);
    Serial.print(F("%  Moist=")); Serial.print(lastMoist);
    Serial.print(F("  Dist=")); Serial.print(lastDistanceCM);
    Serial.print(F("cm  Armed=")); Serial.println(sysArmed ? F("Y") : F("N"));
  }

  //  Alert logic (event- & distance-based)
  if (tamperEvent && sysArmed) {
    alertActive = true;
    alertClearMs = now + 5000;
    Serial.println(F("[ALERT] Tamper triggered!"));
    tamperEvent = false;
  }
  if (sysArmed && lastDistanceCM > 0 && lastDistanceCM < DIST_CLOSE_CM) {
    alertActive = true;
    alertClearMs = now + 5000;
    static unsigned long lastDistAlertPrint = 0;
    if (now - lastDistAlertPrint > 1000) {
      Serial.println(F("[ALERT] Close object detected!"));
      lastDistAlertPrint = now;
    }
  }
  if (alertActive && now >= alertClearMs) {
    alertActive = false;
    Serial.println(F("[ALERT] Cleared"));
  }

  // Drive LEDs

  // RED: blink rapidly when alert is active (but stop instantly if disarmed)
  if (alertActive && sysArmed) {
    static unsigned long lastRedBlink = 0;
    static bool redOn = false;
    if (now - lastRedBlink >= 100) {  // toggle every 100 ms
      lastRedBlink = now;
      redOn = !redOn;
      digitalWrite(LED_RED, redOn ? HIGH : LOW);
    }
  } else {
    digitalWrite(LED_RED, LOW);
  }

  // GREEN: system status (solid ON if armed)
  digitalWrite(LED_GREEN, sysArmed ? HIGH : LOW);

  // ORANGE: heartbeat (faster if env warning)
  unsigned long period = envWarning ? 500 : 1000;
  if (now - lastBlinkMs >= period) {
    lastBlinkMs = now;
    orangeOn = !orangeOn;
  }
  digitalWrite(LED_ORANGE, orangeOn ? HIGH : LOW);
} // <-- and the closing brace was already here

//  Main loop 
void loop() {
  if (g_pcintFired) { noInterrupts(); g_pcintFired = false; interrupts(); handlePCINTEdges(); }
  if (g_timerTick)  { noInterrupts(); g_timerTick  = false; interrupts(); runSchedulers(); }
}

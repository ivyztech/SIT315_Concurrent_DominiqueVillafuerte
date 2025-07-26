const byte LED_PIN = 13;
const byte METER_PIN = A4;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(METER_PIN, INPUT);
  
  Serial.begin(9600);
  
  // Initial frequency setup
  double timerFrequency = readFrequencyFromPot();
  startTimer(timerFrequency);
}

void loop() {
  double timerFrequency = readFrequencyFromPot();
  startTimer(timerFrequency);
  delay(1000); // Updating frequency every second
}

void startTimer(double timerFrequency) {
  noInterrupts();  // Disable interrupts

  // Clearing Timer1 registers
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;

  // Calculating the value for the compare match register
  double ocrValue = (16000000 / (1024 * timerFrequency)) - 1;
  OCR1A = (unsigned long)ocrValue;

  // Setting CTC mode and prescaler of 1024
  TCCR1B |= (1 << WGM12);  // CTC mode
  TCCR1B |= (1 << CS12) | (1 << CS10);  // 1024 prescaler

  // Enabling Timer1 compare interrupt
  TIMSK1 |= (1 << OCIE1A);

  interrupts();  // Enabling interrupts
}

ISR(TIMER1_COMPA_vect) {
  digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);
}

double readFrequencyFromPot() {
  int sensorValue = analogRead(METER_PIN);
  double minFreq = 0.5;
  double maxFreq = 2.0;
  double frequency = minFreq + (sensorValue / 1023.0) * (maxFreq - minFreq);
  return frequency;
}

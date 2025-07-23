
//Pins connected to the button and the LED
const uint8_t BTN_PIN = 2;
const uint8_t LED_PIN = 13;

//Variables to store the current state of the LED
uint8_t ledState = LOW;


void setup()
{
  //Setting button pin as input with the pull-up resistor
  pinMode(BTN_PIN, INPUT_PULLUP);
  //Setting LED pin as output
  pinMode(LED_PIN, OUTPUT);
  //Initialising serial communication for debugging
  Serial.begin(9600);

  // Attaching an Interrupt
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), sensorISR, FALLING);
}

void loop()
{
   //Printing states
  Serial.print(buttonState);
  Serial.print(buttonPrevState);
  Serial.print(ledState);
  Serial.println("");

  //Small delay to avoid flooding the serial output
  delay(500);
  

}

// Interrupt service routine (ISR) for handling the button press
void sensorISR()
{
  //Toggle the LED state
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState);
  
}
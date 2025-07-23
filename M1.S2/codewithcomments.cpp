
//Pins connected to the button and the LED
const uint8_t BTN_PIN = 2;
const uint8_t LED_PIN = 13;

//Variables to store the previous state of the button and the current state of the LED
uint8_t buttonPrevState = LOW;
uint8_t ledState = LOW;


void setup()
{
  //Setting button pin as input with the pull-up resistor
  pinMode(BTN_PIN, INPUT_PULLUP);
  //Setting LED pin as output
  pinMode(LED_PIN, OUTPUT);
  //Initialising serial communication for debugging
  Serial.begin(9600);
}

void loop()
{
  //Reading current stte of the button
  uint8_t buttonState = digitalRead(BTN_PIN);
  
  //Printing states
  Serial.print(buttonState);
  Serial.print(buttonPrevState);
  Serial.print(ledState);
  Serial.println("");
  
  
  //Checking if the button state has changed for the LED to be toggled
  if(buttonState != buttonPrevState)
  {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
  
  //Update the previous button state
  buttonPrevState = buttonState;
    
  //Small delay to debounce the button
  delay(500);
}
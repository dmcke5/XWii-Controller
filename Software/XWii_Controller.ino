#include <EEPROM.h>
#include <XInput.h>
#include <FastLED.h>

#define NUM_LEDS 1
#define DATA_PIN LED_BUILTIN_TX

CRGB leds[NUM_LEDS];
XInputLEDPattern ledPattern;
uint8_t playerNo;
int ledState = 2;
unsigned long ledTimer;

//#define PWM47k  3   //  46875 Hz
//#define PWM6        OCR4D
//#define PWM6_13_MAX OCR4C

//Default Joystick calibration settings and EEPROM storage Address
int minLeftX = 0; //EEPROM Adr = 1
int maxLeftX = 720; //EEPROM Adr = 3
int midLeftX = 360; //EEPROM Adr = 5

int minLeftY = 0; //EEPROM Adr = 7
int maxLeftY = 600; //EEPROM Adr = 9
int midLeftY = 390; //EEPROM Adr = 11

int minRightY = 0; //EEPROM Adr = 13
int maxRightY = 760; //EEPROM Adr = 15
int midRightY = 400; //EEPROM Adr = 17

int minRightX = 0; //EEPROM Adr = 19
int maxRightX = 760; //EEPROM Adr = 21
int midRightX = 390; //EEPROM Adr = 23

int minL2 = 10;  //EEPROM Adr = 25
int maxL2 = 230;  //EEPROM Adr = 27
int minR2 = 10;  //EEPROM Adr = 29
int maxR2 = 230; //EEPROM Adr = 31

//Joystick LookUp Tables, compiled at startup or after joystick calibration.
byte leftXLUT[400];
byte leftYLUT[400];
byte rightXLUT[400];
byte rightYLUT[400];

float scaleFactor;    //-----------------------------
int scaleMax = 255;   //Used for LUT calculations
int lutLength = 400;  //-----------------------------

//Joystick Settings
const bool invertLeftY = true;        //------------------------------------------
const bool invertLeftX = false;       //Change these settings for Inverted mounting 
const bool invertRightY = false;      //of joysticks.
const bool invertRightX = false;      //------------------------------------------
int invertLeftZ = 0;             //Set automatically during calibration. Stored: EEPROM Adr = 33
int invertRightZ = 0;            //Set automatically during calibration. Stored: EEPROM Adr = 35
const int deadBand = 10;              //Joystick deadband settings. 
const bool useDeadband = true;        //
const int earlyStop = 30;             //Distance from end of travel to achieve full axis movement.
const int triggerDeadband = 5;       //Trigger Deadband

boolean calibrationMode = false;  //Set to true to open Calibration menu
int calibrationStep = 1;          //Stage of calibration process
int leftZNeutral;                 //Used during calibration process to record trigger neutral position
int rightZNeutral;                //and correct for reversed magnet orientation.

//Controller IO setup
int buttonCount = 15;                   //Number of buttons connected
byte buttonPins[15] = {15,16, 14, 8, 11, 5, 13, 3, 2, 0, 1, 4, 12, 6, 7}; //R1, Minus, R3, L3, Right, Up, Left, A, B, X, Y, Plus, Home, L1, Down,  

#define leftZ A0
#define leftX A2
#define leftY A1
#define rightZ A3
#define rightX A5
#define rightY A4

byte smallMotor = 9;
byte largeMotor = 10;

//Button state arrays
byte lastButtonState[15]; //Empty State array for buttons last sent state.
byte currentButtonState[15]; //Empty State array for buttons.

//Current rumble values
uint8_t rumbleLeft = 0;
uint8_t rumbleRight = 0;

void setup() {
  Serial.begin(9600);

  FastLED.addLeds<WS2811, DATA_PIN, GRB>(leds, NUM_LEDS);

  XInput.setAutoSend(false);  // Wait for all controls before sending
  XInput.begin();
  XInput.setReceiveCallback(xInputCallback);

  while(!Serial);

  for(int i = 0; i < buttonCount; i++){ //Set all button pins as input pullup. Change to INPUT if using external resistors.
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  pinMode(leftZ, INPUT); //Set input for left trigger
  pinMode(leftX, INPUT); //Set input for left joystick X
  pinMode(leftY, INPUT); //Set input for left joystick Y
  pinMode(rightZ, INPUT); //Set input for right trigger
  pinMode(rightX, INPUT); //Set input for right joystick X
  pinMode(rightY, INPUT); //Set input for right joystick Y

  //pwm613configure(PWM47k);
  //pwmSet6(0);
  pinMode(smallMotor, OUTPUT);
  pinMode(largeMotor, OUTPUT);

  eepromLoad();
  
  buildLUTs();
  XInput.setJoystickRange(0, scaleMax);

  playerNo = XInput.getPlayer();
}

void loop() {
  buttonRead();
  if(calibrationMode){
    joystickCalibration();
    if(ledTimer + 1000 < millis()){
      if(ledState == 0){
        ledState = 1;
        leds[0] = CRGB::Blue;
      } else {
        ledState = 0;
        leds[0] = CRGB::Black;
      }
      ledTimer = millis();
      FastLED.show();
    }
  } else {
    gamepadMode();
  }
  if(lastButtonState[1] == 1 & lastButtonState[11] == 1 & lastButtonState[2] == 1){ //Minus, Plus, R3
    calibrationMode = true;
  }
  if(ledState == 2){
    leds[0] = CRGB::Blue;
    FastLED.show();
    ledState = 0;
  }
  //Serial.println("Test");
}

void gamepadMode(){ //Controller Gamepad mode
  joystickInput();
  //Buttons
  for(int i = 0; i < buttonCount; i++){
    if(lastButtonState[i] != currentButtonState[i]){
      currentButtonState[i] = lastButtonState[i];
    }
  }
  if(rumbleRight > 0){
    analogWrite(smallMotor, rumbleRight);
    //PWM6 = rumbleLeft;
  } else {
    //PWM6 = 0;
    digitalWrite(smallMotor, 0);
  }
  if(rumbleLeft > 0){
    analogWrite(largeMotor, rumbleLeft);
  } else {
    digitalWrite(largeMotor, 0);
  }

  XInput.setButton(BUTTON_A, lastButtonState[7]);
	XInput.setButton(BUTTON_B, lastButtonState[8]);
	XInput.setButton(BUTTON_X, lastButtonState[9]);
	XInput.setButton(BUTTON_Y, lastButtonState[10]);

	XInput.setButton(BUTTON_LB, lastButtonState[13]);
	XInput.setButton(BUTTON_RB, lastButtonState[0]);

	XInput.setButton(BUTTON_BACK, lastButtonState[1]);
	XInput.setButton(BUTTON_START, lastButtonState[11]);
  XInput.setButton(BUTTON_LOGO, lastButtonState[12]);

	XInput.setButton(BUTTON_L3, lastButtonState[3]);
	XInput.setButton(BUTTON_R3, lastButtonState[2]);

  XInput.setDpad(lastButtonState[5], lastButtonState[14], lastButtonState[6], lastButtonState[4]);
  XInput.send();
}

void joystickInput(){ //Read joysticks and compare to LUT's . Read triggers and scale output
  int var = analogRead(leftY) / (float)scaleFactor;
  XInput.setJoystickY(JOY_LEFT, leftYLUT[var]);

  var = analogRead(leftX) / (float)scaleFactor;
  XInput.setJoystickX(JOY_LEFT, leftXLUT[var]);
  
  var = analogRead(rightY) / (float)scaleFactor;
  if(var != 0){
    XInput.setJoystickY(JOY_RIGHT, rightYLUT[var]);
  }
  
  var = analogRead(rightX) / (float)scaleFactor;
  if(var != 0){
    XInput.setJoystickX(JOY_RIGHT, rightXLUT[var]);
  }

  var = analogRead(rightZ);
  if(invertRightZ){
    if(var < maxR2 - triggerDeadband){ //Skip scaling and just output minimum if trigger isn't pressed to save time
      var = triggerScale(var, minR2, maxR2, invertRightZ);
      XInput.setTrigger(TRIGGER_RIGHT, var);
    } else {
      XInput.setTrigger(TRIGGER_RIGHT, 0);
    }
  } else {
    if(var > minR2 + triggerDeadband){ //Skip scaling and just output minimum if trigger isn't pressed to save time
      var = triggerScale(var, minR2, maxR2, invertRightZ);
      XInput.setTrigger(TRIGGER_RIGHT, var);
    } else {
      XInput.setTrigger(TRIGGER_RIGHT, 0);
    }
  }

  var = analogRead(leftZ);
  if(invertLeftZ){
    if(var < maxL2 - triggerDeadband){  //Skip scaling and just output minimum if trigger isn't pressed to save time
      var = triggerScale(var, minL2, maxL2, invertLeftZ);
      XInput.setTrigger(TRIGGER_LEFT, var);
    } else {
      XInput.setTrigger(TRIGGER_LEFT, 0);
    }
  } else {
    if(var > minL2 + triggerDeadband){  //Skip scaling and just output minimum if trigger isn't pressed to save time
      var = triggerScale(var, minL2, maxL2, invertLeftZ);
      XInput.setTrigger(TRIGGER_LEFT, var);
    } else {
      XInput.setTrigger(TRIGGER_LEFT, 0);
    }
  }
}

void buttonRead(){ //Read button inputs and set state arrays.
  for (int i = 0; i < buttonCount; i++){
    int input = !digitalRead(buttonPins[i]);
    if (input != lastButtonState[i]){
      lastButtonState[i] = input;
    }
  }
}
//6,4,200,1
int triggerScale(int value, int min, int max, int invert){  //Scales input "value" to 0 - 254 for correct output whilst taking into account deadband.
//int temp;
  if(value > max){
    value = max;
  }
  if(value < min){
    value = min;
  }

  if(!invert){
    return map(value, min, max, 0, 254);
  } else {
    return map(value, max, min, 0, 254);
  }
}

void eepromLoad(){ //Loads stored settings from EEPROM
  if(readIntFromEEPROM(1) != -1){ //Check Joystick Calibration in EEPROM is not Empty
    readCalibrationData(); //Load joystick calibration from EEPROM
  }
}

void writeIntIntoEEPROM(int address, int number){ //Splits int "number" into BYTES and writes to EEPROM at given address
  byte byte1 = number >> 8;
  byte byte2 = number & 0xFF;
  EEPROM.write(address, byte1);
  EEPROM.write(address + 1, byte2);
}

int readIntFromEEPROM(int address){ //Returns int value from EEPROM adress +1
  byte byte1 = EEPROM.read(address);
  byte byte2 = EEPROM.read(address + 1);
  return (byte1 << 8) + byte2;
}

void readCalibrationData(){ //Read calibration data from EEPROM
//Left X
  minLeftX = readIntFromEEPROM(1);
  maxLeftX = readIntFromEEPROM(3);
  midLeftX = readIntFromEEPROM(5);
//Left Y
  minLeftY = readIntFromEEPROM(7);
  maxLeftY = readIntFromEEPROM(9);
  midLeftY = readIntFromEEPROM(11);
//Right Y
  minRightY = readIntFromEEPROM(13);
  maxRightY = readIntFromEEPROM(15);
  midRightY = readIntFromEEPROM(17);
// Right X
  minRightX = readIntFromEEPROM(19);
  maxRightX = readIntFromEEPROM(21);
  midRightX = readIntFromEEPROM(23);
// L2
  minL2 = readIntFromEEPROM(25);
  maxL2 = readIntFromEEPROM(27);
  invertLeftZ = readIntFromEEPROM(33);
// R2
  minR2 = readIntFromEEPROM(29);
  maxR2 = readIntFromEEPROM(31);
  invertRightZ = readIntFromEEPROM(35);
}

void writeCalibrationData(){ //Store calibration data in EEPROM
//Left X
  writeIntIntoEEPROM(1, minLeftX);
  writeIntIntoEEPROM(3, maxLeftX);
  writeIntIntoEEPROM(5, midLeftX);
//Left Y
  writeIntIntoEEPROM(7, minLeftY);
  writeIntIntoEEPROM(9, maxLeftY);
  writeIntIntoEEPROM(11, midLeftY);
//Right Y
  writeIntIntoEEPROM(13, minRightY);
  writeIntIntoEEPROM(15, maxRightY);
  writeIntIntoEEPROM(17, midRightY);
// Right X
  writeIntIntoEEPROM(19, minRightX);
  writeIntIntoEEPROM(21, maxRightX);
  writeIntIntoEEPROM(23, midRightX);
// L2
  writeIntIntoEEPROM(25, minL2);
  writeIntIntoEEPROM(27, maxL2);
  writeIntIntoEEPROM(33, invertLeftZ);
// R2
  writeIntIntoEEPROM(29, minR2);
  writeIntIntoEEPROM(31, maxR2);
  writeIntIntoEEPROM(35, invertRightZ);
}

void lutScaleFactor(){ //Calculates "scale factor" to ensure maximum resolution of LUT's with given memory limitations.
  int largestValue = 0;
  if(maxLeftX > largestValue){
    largestValue = maxLeftX;
  }
  if(maxLeftY > largestValue){
    largestValue = maxLeftY;
  }
  if(maxRightX > largestValue){
    largestValue = maxRightX;
  }
  if(maxRightY > largestValue){
    largestValue = maxRightY;
  }
  scaleFactor = largestValue / (float)lutLength;
}

void buildLUTs(){ //Builds the look up table for each joystick Axis
  lutScaleFactor(); //Calulate Scale Factor for LUT's

  //Left X
  for(int i = 0; i < lutLength; i++){
    leftXLUT[i] = joystickScale(i * scaleFactor, minLeftX, midLeftX, maxLeftX);
  }
  //Left Y
  for(int i = 0; i < lutLength; i++){
    leftYLUT[i] = joystickScale(i * scaleFactor, minLeftY, midLeftY, maxLeftY);
  }
  //Right X
  for(int i = 0; i < lutLength; i++){
    rightXLUT[i] = joystickScale(i * scaleFactor, minRightX, midRightX, maxRightX);
  }
  //Right Y
  for(int i = 0; i < lutLength; i++){
    rightYLUT[i] = joystickScale(i * scaleFactor, minRightY, midRightY, maxRightY);
  }
}

int joystickScale(int value, int min, int mid, int max){
  int output;
  if(value < mid - deadBand){ //Value is less than halfway and outside deadband
    output = map(value, min + earlyStop, mid, 0, scaleMax / 2);
    if(output < 0){
      output = 0;
    }
  } else if(value > mid + deadBand){ //Value is greater than halfway and outside deadband
    output = map(value, mid, max - earlyStop, scaleMax / 2, scaleMax);
    if(output > scaleMax){
      output = scaleMax;
    }
  } else { //Value is within deadband.
    output = scaleMax / 2;
  }
  return output;
}

void joystickCalibration(){ //Joystick Calibration Sequence

  if(calibrationStep == 1){
    buttonRead();
    if(lastButtonState[7] == 1){
      midRightX = analogRead(rightX);
      midRightY = analogRead(rightY);
      midLeftX = analogRead(leftX);
      midLeftY = analogRead(leftY);
      leftZNeutral = analogRead(leftZ);
      rightZNeutral = analogRead(rightZ);
      delay(50);
      minLeftX = midLeftX;
      minLeftY = midLeftY;
      maxLeftX = 0;
      maxLeftY = 0;
      minRightX = midRightX;
      minRightY = midRightY;
      maxRightX = 0;
      maxRightY = 0;
      calibrationStep = 2;
      Serial.println(leftZNeutral);
      digitalWrite(smallMotor, HIGH);
      delay(100);
      digitalWrite(smallMotor, LOW);
      delay(150);
    }
  } else if(calibrationStep == 2){
    int var = analogRead(leftX);
    if(var != 0){
      if(var > maxLeftX) maxLeftX = var;
      if(var < minLeftX) minLeftX = var;
    }
    var = analogRead(leftY);
    if(var != 0){
      if(var > maxLeftY) maxLeftY = var;
      if(var < minLeftY) minLeftY = var;
    }
    var = analogRead(rightX);
    if(var != 0){
      if(var > maxRightX) maxRightX = var;
      if(var < minRightX) minRightX = var;
    }
    var = analogRead(rightY);
    if(var != 0){
      if(var > maxRightY) maxRightY = var;
      if(var < minRightY) minRightY = var;
    }
    buttonRead();
    if(lastButtonState[7] == 1){
      calibrationStep = 3;
      minL2 = 1023;
      maxL2 = 0;
      minR2 = 1023;
      maxR2 = 0;
      digitalWrite(smallMotor, HIGH);
      delay(100);
      digitalWrite(smallMotor, LOW);
      delay(150);
    }
  } else if(calibrationStep == 3){
    int var = analogRead(rightZ);
    if(var != 0){
      if(var > maxR2) maxR2 = var;
      if(var < minR2) minR2 = var;
    }
    var = analogRead(leftZ); //L2
    if(var != 0){
      if(var > maxL2) maxL2 = var;
      if(var < minL2) minL2 = var;
    }
    if(lastButtonState[7] == 1){ //Complete Calibration
    Serial.println(minL2);
    Serial.println(maxL2);
      if(leftZNeutral > minL2){
        invertLeftZ = 1;
        Serial.println("Left Inverted");
      } else {
        invertLeftZ = 0;
      }
      if(rightZNeutral > minR2){
        invertRightZ = 1;
        Serial.println("Right Inverted");
      } else {
        invertRightZ = 0;
      }
      digitalWrite(smallMotor, HIGH);
      delay(100);
      digitalWrite(smallMotor, LOW);
      delay(150);
      writeCalibrationData(); //Update EEPROM
      buildLUTs();
      delay(50);
      calibrationStep = 1;
      calibrationMode = false;
      leds[0] = CRGB::Blue;
      FastLED.show();
    }
  }
}

void xInputCallback(uint8_t packetType) {
	if (packetType == (uint8_t) XInputReceiveType::LEDs) {
		ledPattern = XInput.getLEDPattern();
	}

/*

XInput LED Patterns (currently unused):
  Off = 0x00
	Blinking = 0x01
	Flash1 = 0x02
	Flash2 = 0x03
	Flash3 = 0x04
	Flash4 = 0x05
	On1 = 0x06
	On2 = 0x07
	On3 = 0x08
	On4 = 0x09
	Rotating = 0x0A
	BlinkOnce = 0x0B
	BlinkSlow = 0x0C
	Alternating = 0x0D
  
*/
	else if (packetType == (uint8_t) XInputReceiveType::Rumble) {
		rumbleLeft = XInput.getRumbleLeft();
    rumbleRight = XInput.getRumbleRight();
	}
}

void pwm613configure(int mode) {
  TCCR4A=0;// TCCR4A configuration
  TCCR4B=mode;// TCCR4B configuration
  TCCR4C=0;// TCCR4C configuration
  TCCR4D=0;// TCCR4D configuration
  TCCR4D=0;// TCCR4D configuration

  // PLL Configuration
  // Use 96MHz / 2 = 48MHz
  PLLFRQ=(PLLFRQ&0xCF)|0x30;
  // PLLFRQ=(PLLFRQ&0xCF)|0x10; // Will double all frequencies

  OCR4C=255;// Terminal count for Timer 4 PWM
}

void pwmSet6(int value) {
  OCR4D=value;   // Set PWM value
  DDRD|=1<<7;    // Set Output Mode D7
  TCCR4C|=0x09;  // Activate channel D
}


#include <Arduino.h>
#include <EasyLogger.h>
#include <NewPing.h>
#include <Adafruit_NeoPixel.h>

const byte triggerPin = 9;
const byte echoPin = 10;
const byte echoCount = 1;
const byte stripPin = 8;
const byte maxDistance = 200;  //In CM, everything else inches

const uint32_t red = 0xFF0000;
const uint32_t yellow = 0xFFFF00;
const uint32_t green = 0x00FF00;
const uint32_t black = 0x000000;

const int ledCount = 30;

//Bits
const byte BIT_ON = 1 << 7;
const byte BIT_STOP = 1 << 6;
const byte BIT_SLOW = 1 << 5;
const byte BIT_GO = 1 << 4;

const byte MASK_SUBSTANTIVE = BIT_STOP | BIT_SLOW | BIT_GO;

enum ParkingState {
  OFF_FARAWAY,
  WELCOMING = BIT_ON | BIT_GO | 1,
  KEEP_GOING = BIT_ON | BIT_GO | 2,

  SLOW_DOWN_1 = BIT_ON | BIT_SLOW | 1,
  SLOW_DOWN_2 = BIT_ON | BIT_SLOW | 2,

  STOP_1 = BIT_ON | BIT_STOP | 1,
  STOP_2 = BIT_ON | BIT_STOP | 2,
  STOP_3 = BIT_ON | BIT_STOP | 3,

  OFF_LONG_HERE
};

enum DistanceThresholds {
  stop3 = 5,
  stop2 = 10,
  stop1 = 15,

  slow2 = 20,
  slow1 = 25,

  keepGoing = 50,
};

void setState(ParkingState state);
void setupLed();
void displayState();
void turnAllOff();
void animateWelcome();

Adafruit_NeoPixel strip = Adafruit_NeoPixel(ledCount, stripPin, NEO_GRB + NEO_KHZ800);
bool welcomeState[ledCount];
ParkingState currentState;
NewPing sonar(triggerPin, echoPin, maxDistance);
int welcomeDelayCounter = 0;
unsigned long lastSubstantialChangeTime = millis();
unsigned long ledTimeout = 5 * 60 * 1000; //5 minutes
//unsigned long ledTimeout = 30 * 1000;  // 30 seconds - for testing

void setup() {
  Serial.begin(115200);

  setupLed();

  LOG_INFO("SETUP", "Setup Complete")
}


void setupLed(){
  LOG_INFO("setupLed", "LED setup")

  strip.begin();
  strip.setBrightness(40);
  strip.fill(strip.Color(255, 0, 255), 0, 10);
  strip.show();  // Initialize all pixels to 'off'

  //Init welcome state pattern
  for (int x = 0; x < ledCount; x++) {
    if (x % 3 == 0) {
      welcomeState[x] = true;
    }
  }

  LOG_INFO("setupLed", "Done with LED setup")
}


void loop() {
  long distance = sonar.ping_in();
  unsigned long startTime = millis();
  if (distance == 0) {
    distance = maxDistance;
  }
  
  if (distance < DistanceThresholds::stop3) {
    setState(ParkingState::STOP_3);
  } else if (distance < DistanceThresholds::stop2) {
    setState(ParkingState::STOP_2);
  } else if (distance < DistanceThresholds::stop1) {
    setState(ParkingState::STOP_1);
  } else if (distance < DistanceThresholds::slow2) {
    setState(ParkingState::SLOW_DOWN_2);
  } else if (distance < DistanceThresholds::slow1) {
    setState(ParkingState::SLOW_DOWN_1);
  } else if (distance < DistanceThresholds::keepGoing) {
    setState(ParkingState::KEEP_GOING);
  } else {
    setState(ParkingState::WELCOMING);
  }

  if (currentState == ParkingState::WELCOMING) {
    if (welcomeDelayCounter == 0) {
      displayState();
    }
    welcomeDelayCounter++;
    if (welcomeDelayCounter >= 4) {
      welcomeDelayCounter = 0;
      delay(50);
    } else {
      delay(100);
    }
  } else {
    displayState();
    delay(50);
  }
}


void setState(ParkingState state) {
  bool substantiveChange = false;

  if ((currentState & MASK_SUBSTANTIVE) != (state & MASK_SUBSTANTIVE)) {
    substantiveChange = true;
    welcomeDelayCounter = 0;  //Reset
    lastSubstantialChangeTime = millis();
  }

  currentState = state;
}

void displayState() {
  unsigned long timeSinceLastChange = millis() - lastSubstantialChangeTime;
  if (timeSinceLastChange > ledTimeout) {
    turnAllOff();
    return;
  }

  if (currentState == ParkingState::STOP_3) {
    strip.fill(red, 0, ledCount);
    strip.show();
  } else if (currentState == ParkingState::STOP_2) {
    strip.fill(black, 0, 5);
    strip.fill(red, 5, 20);
    strip.fill(black, 25, 5);
    strip.show();
  } else if (currentState == ParkingState::STOP_1) {
    strip.fill(black, 0, 10);
    strip.fill(red, 10, 10);
    strip.fill(black, 20, 10);
    strip.show();
  } else if (currentState == ParkingState::SLOW_DOWN_2) {
    strip.fill(black, 0, 11);
    strip.fill(yellow, 11, 8);
    strip.fill(black, 19, 11);
    strip.show();
  } else if (currentState == ParkingState::SLOW_DOWN_1) {
    strip.fill(black, 0, 12);
    strip.fill(yellow, 12, 6);
    strip.fill(black, 18, 12);
    strip.show();
  } else if (currentState == ParkingState::KEEP_GOING) {
    strip.fill(green, 0, 30);
    strip.show();
  } else if (currentState == ParkingState::WELCOMING) {
    animateWelcome();
  } else if (currentState & BIT_ON == 0) {
    strip.fill(black, 0, 30);
    strip.show();
  }
}

void animateWelcome() {
  bool first = welcomeState[0];

  for (int x = 0; x < ledCount - 1; x++) {
    welcomeState[x] = welcomeState[x + 1];
    uint32_t color = welcomeState[x] ? green : black;
    strip.setPixelColor(x, color);
  }

  welcomeState[ledCount-1] = first;
  uint32_t color = welcomeState[ledCount] ? green : black;
  strip.setPixelColor(ledCount, color);
  strip.show();
}

void turnAllOff() {
  strip.fill(black, 0, ledCount);
  strip.show();
}
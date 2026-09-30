#include <Arduino.h>
#include <Bluepad32.h>
#include <ESP32Servo.h>

ControllerPtr myControllers[BP32_MAX_GAMEPADS];
Servo turnServo;
Servo speedServo;

const int MOTOR_1A = A2;
const int MOTOR_2A = A3;
const int ENGINE_PIN_1 = 11;
const int ENGINE_PIN_2 = 12;
const int TURN_SERVO_PIN = 5;
const int SPEED_SERVO_PIN = 6;

enum DISPLAY_PIN {
  A = 47,
  B = 38,
  C = 10,
  D = 8,
  E = 9,
  F = 21,
  G = 18,
  DP = 17
};

const int TURN_LIMIT_ANGLE[2] = {15, 160};
const int MIN_SPEED = 45;

const int GEAR_L_POSITION = 72;
const int GEAR_H_POSITION = 144;

const int MIN_ENGINE_SPEED = 60;
bool turnOffEngine = false;

// This callback gets called any time a new gamepad is connected.
// Up to 4 gamepads can be connected at the same time.
void onConnectedController(ControllerPtr ctl) {
    bool foundEmptySlot = false;
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            Serial.printf("CALLBACK: Controller is connected, index=%d\n", i);
            digitalWrite(DISPLAY_PIN::DP, HIGH);
            // Additionally, you can get certain gamepad properties like:
            // Model, VID, PID, BTAddr, flags, etc.
            ControllerProperties properties = ctl->getProperties();
            Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", ctl->getModelName().c_str(), properties.vendor_id,
                           properties.product_id);
            myControllers[i] = ctl;
            foundEmptySlot = true;
            runEngine(MIN_ENGINE_SPEED);
            break;
        }
    }
    if (!foundEmptySlot) {
        Serial.println("CALLBACK: Controller connected, but could not found empty slot");
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    bool foundController = false;
    runEngine(0);

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            Serial.printf("CALLBACK: Controller disconnected from index=%d\n", i);
            digitalWrite(DISPLAY_PIN::DP, LOW);
            myControllers[i] = nullptr;
            foundController = true;
            break;
        }
    }

    if (!foundController) {
        Serial.println("CALLBACK: Controller disconnected, but not found in myControllers");
    }
}

void dumpGamepad(ControllerPtr ctl) {
    Serial.printf(
        "idx=%d, dpad: 0x%02x, buttons: 0x%04x, axis L: %4d, %4d, axis R: %4d, %4d, brake: %4d, throttle: %4d, "
        "misc: 0x%02x, gyro x:%6d y:%6d z:%6d, accel x:%6d y:%6d z:%6d\n",
        ctl->index(),        // Controller Index
        ctl->dpad(),         // D-pad
        ctl->buttons(),      // bitmask of pressed buttons
        ctl->axisX(),        // (-511 - 512) left X Axis
        ctl->axisY(),        // (-511 - 512) left Y axis
        ctl->axisRX(),       // (-511 - 512) right X axis
        ctl->axisRY(),       // (-511 - 512) right Y axis
        ctl->brake(),        // (0 - 1023): brake button
        ctl->throttle(),     // (0 - 1023): throttle (AKA gas) button
        ctl->miscButtons(),  // bitmask of pressed "misc" buttons
        ctl->gyroX(),        // Gyro X
        ctl->gyroY(),        // Gyro Y
        ctl->gyroZ(),        // Gyro Z
        ctl->accelX(),       // Accelerometer X
        ctl->accelY(),       // Accelerometer Y
        ctl->accelZ()        // Accelerometer Z
    );
}

void processGamepad(ControllerPtr ctl) {
    // There are different ways to query whether a button is pressed.
    // By query each button individually:
    //  a(), b(), x(), y(), l1(), etc...
    if (ctl->a()) {
        static int colorIdx = 0;
        // Some gamepads like DS4 and DualSense support changing the color LED.
        // It is possible to change it by calling:
        switch (colorIdx % 3) {
            case 0:
                // Red
                ctl->setColorLED(255, 0, 0);
                break;
            case 1:
                // Green
                ctl->setColorLED(0, 255, 0);
                break;
            case 2:
                // Blue
                ctl->setColorLED(0, 0, 255);
                break;
        }
        colorIdx++;
    }

    if (ctl->b()) {
        // Turn on the 4 LED. Each bit represents one LED.
        static int led = 0;
        led++;
        // Some gamepads like the DS3, DualSense, Nintendo Wii, Nintendo Switch
        // support changing the "Player LEDs": those 4 LEDs that usually indicate
        // the "gamepad seat".
        // It is possible to change them by calling:
        ctl->setPlayerLEDs(led & 0x0f);
    }

    if (ctl->x()) {
        // Some gamepads like DS3, DS4, DualSense, Switch, Xbox One S, Stadia support rumble.
        // It is possible to set it by calling:
        // Some controllers have two motors: "strong motor", "weak motor".
        // It is possible to control them independently.
        ctl->playDualRumble(0 /* delayedStartMs */, 250 /* durationMs */, 0x80 /* weakMagnitude */,
                            0x40 /* strongMagnitude */);
    }

    // Another way to query controller data is by getting the buttons() function.
    // See how the different "dump*" functions dump the Controller info.
    dumpGamepad(ctl);
    controlCar(ctl);
}

void processControllers() {
    for (auto myController : myControllers) {
        if (myController && myController->isConnected() && myController->hasData()) {
            if (myController->isGamepad()) {
                processGamepad(myController);
            }
        }
    }
}

// Arduino setup function. Runs in CPU 1
void setup() {
    Serial.begin(115200);
    Serial.printf("Firmware: %s\n", BP32.firmwareVersion());

    turnServo.attach(TURN_SERVO_PIN);
    turnServo.write(90);

    speedServo.attach(SPEED_SERVO_PIN);
    speedServo.write(GEAR_H_POSITION);

    pinMode(MOTOR_1A, OUTPUT);
    pinMode(MOTOR_2A, OUTPUT);

    pinMode(ENGINE_PIN_1, OUTPUT);
    pinMode(ENGINE_PIN_2, OUTPUT);

    pinMode(DISPLAY_PIN::A, OUTPUT);
    pinMode(DISPLAY_PIN::B, OUTPUT);
    pinMode(DISPLAY_PIN::C, OUTPUT);
    pinMode(DISPLAY_PIN::D, OUTPUT);
    pinMode(DISPLAY_PIN::E, OUTPUT);
    pinMode(DISPLAY_PIN::F, OUTPUT);
    pinMode(DISPLAY_PIN::G, OUTPUT);
    pinMode(DISPLAY_PIN::DP, OUTPUT);

    displayDigitalH();

    const uint8_t* addr = BP32.localBdAddress();
    Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Setup the Bluepad32 callbacks
    BP32.setup(&onConnectedController, &onDisconnectedController);

    // "forgetBluetoothKeys()" should be called when the user performs
    // a "device factory reset", or similar.
    // Calling "forgetBluetoothKeys" in setup() just as an example.
    // Forgetting Bluetooth keys prevents "paired" gamepads to reconnect.
    // But it might also fix some connection / re-connection issues.
    // BP32.forgetBluetoothKeys();

    // Enables mouse / touchpad support for gamepads that support them.
    // When enabled, controllers like DualSense and DualShock4 generate two connected devices:
    // - First one: the gamepad
    // - Second one, which is a "virtual device", is a mouse.
    // By default, it is disabled.
    BP32.enableVirtualDevice(false);
}

// Arduino loop function. Runs in CPU 1.
void loop() {
    // This call fetches all the controllers' data.
    // Call this function in your main loop.
    bool dataUpdated = BP32.update();
    if (dataUpdated)
        processControllers();

    // The main loop must have some kind of "yield to lower priority task" event.
    // Otherwise, the watchdog will get triggered.
    // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
    // Detailed info here:
    // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time

    // vTaskDelay(1);

    delay(50); // 150 - previous value
}

void controlCar(ControllerPtr ctl) {
    const int leftAxisY = ctl->axisY(); // (-511 - 512) left Y axis
    const int rightAxisX = ctl->axisRX(); // (-511 - 512) right X axis
    const int buttons = ctl->buttons();

    if (buttons == BUTTON_SHOULDER_R) {
      speedServo.write(GEAR_H_POSITION);
      displayDigitalH();
      Serial.printf("SET SPEED H\n");
    } else if (buttons == BUTTON_SHOULDER_L) {
      speedServo.write(GEAR_L_POSITION);
      displayDigitalL();
      Serial.printf("SET SPEED L\n");
    }

    if (buttons == BUTTON_X) {
      turnOffEngine = !turnOffEngine;
    }

    if (leftAxisY < 0) {
      const int forwardSpeed = map(leftAxisY, 0, -511, MIN_SPEED, 255);
      const int engineSpeed = map(leftAxisY, 0, -511, MIN_ENGINE_SPEED, 255);
      driveForward(forwardSpeed);
      runEngine(engineSpeed);
    } else if (leftAxisY > 0) {
      const int backwardSpeed = map(leftAxisY, 0, 512, MIN_SPEED, 255);
      const int engineSpeed = map(leftAxisY, 0, 512, MIN_ENGINE_SPEED, 255);
      driveBackward(backwardSpeed);
      runEngine(engineSpeed);
    } else {
      stopMotor();
      runEngine(MIN_ENGINE_SPEED);
    }

    if (rightAxisX < 0) {
      const int turnLeftAngle = map(rightAxisX, 0, -511, 90, TURN_LIMIT_ANGLE[0]);
      turn(turnLeftAngle);
    } else if (rightAxisX > 0) {
      const int turnRightAngle = map(rightAxisX, 0, 512, 90, TURN_LIMIT_ANGLE[1]);
      turn(turnRightAngle);
    } else {
      turn(90);
    }
}

void runEngine(int speed)//
{
  Serial.printf("Run Engine\n");

  if (turnOffEngine) {
    analogWrite(ENGINE_PIN_1, 0);
    analogWrite(ENGINE_PIN_2, 0);
  } else {
    analogWrite(ENGINE_PIN_1, 0);
    analogWrite(ENGINE_PIN_2, speed);
  }
}

void driveForward(int speed)//
{
  Serial.printf("driveForward speed=%d\n", speed);
  analogWrite(MOTOR_1A, 0);
  analogWrite(MOTOR_2A, speed);
}

void driveBackward(int speed)//
{
  Serial.printf("driveBackward speed=%d\n", speed);
  analogWrite(MOTOR_1A, speed);
  analogWrite(MOTOR_2A, 0);
}

void stopMotor()
{
  Serial.printf("stopMotor\n");
  analogWrite(MOTOR_1A, 0);
  analogWrite(MOTOR_2A, 0);
}

void turn(int angle)//
{
  Serial.printf("turn angle=%d\n", angle);
  turnServo.write(angle);
}

void displayDigitalH()  //diaplay "H" to the 7-segment
{
  turnOffDisplayAllSegments();
  digitalWrite(DISPLAY_PIN::B, HIGH);
  digitalWrite(DISPLAY_PIN::C, HIGH);
  digitalWrite(DISPLAY_PIN::G, HIGH);
  digitalWrite(DISPLAY_PIN::F, HIGH);
  digitalWrite(DISPLAY_PIN::E, HIGH);
}

void displayDigitalL()  //diaplay "L" to the 7-segment
{
  turnOffDisplayAllSegments();
  digitalWrite(DISPLAY_PIN::F, HIGH);
  digitalWrite(DISPLAY_PIN::E, HIGH);
  digitalWrite(DISPLAY_PIN::D, HIGH);
}

void turnOffDisplayAllSegments() {
  // Turn off all segments of the 7-segment display
  digitalWrite(DISPLAY_PIN::A, LOW);
  digitalWrite(DISPLAY_PIN::B, LOW);
  digitalWrite(DISPLAY_PIN::C, LOW);
  digitalWrite(DISPLAY_PIN::D, LOW);
  digitalWrite(DISPLAY_PIN::E, LOW);
  digitalWrite(DISPLAY_PIN::F, LOW);
  digitalWrite(DISPLAY_PIN::G, LOW);
  // digitalWrite(DISPLAY_PIN::DP, LOW);
}

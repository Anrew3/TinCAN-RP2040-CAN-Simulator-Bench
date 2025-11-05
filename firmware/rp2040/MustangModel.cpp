#include "MustangModel.h"
#include "Config.h"
#include <string.h>   // for memset

static MCP_CAN* canBus = nullptr;

// Default "nothing pressed" message
static unsigned char msgDefault[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// Temporary message for button presses
static unsigned char msgButton[8]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Blinker states and timing
static bool leftBlinkerActive  = false;
static bool rightBlinkerActive = false;
static bool blinkerState       = false;
static unsigned long lastBlinkerToggle = 0;
static const unsigned long blinkerInterval = 500; // ms

// VIN buffer
static char vin[18] = "10203040506070809"; // 17 chars + null
static unsigned char vinMessages[3][8];

// Tire pressures in PSI
static float tirePressuresPSI[4] = {35.0f, 35.0f, 35.0f, 35.0f};
static const char* tireNames[4] = {
  "Driver Front",
  "Passenger Front",
  "Passenger Rear",
  "Driver Rear"
};

// Timing variables
static unsigned long buttonPressStartTime = 0;
static const unsigned long buttonHoldDuration = 100; // ms

static unsigned long lastRPMTime          = 0;
static unsigned long lastSpeedTime        = 0;
static unsigned long lastTirePressureTime = 0;
static unsigned long lastVINMessageTime   = 0;
static unsigned long last3BMessagesTime   = 0;
static unsigned long last109MessageTime   = 0;
static unsigned long lastABSTRACMessageTime = 0;
static unsigned long lastTempMessageTime  = 0;
static unsigned long last81MessageTime    = 0;

// Message intervals
static const unsigned long rpmInterval          = 100;
static const unsigned long speedInterval        = 100;
static const unsigned long tirePressureInterval = 200;
static const unsigned long vinMessageInterval   = 200;
static const unsigned long threeBMessagesInterval = 10;
static const unsigned long message109Interval   = 10;
static const unsigned long absTracInterval      = 10;
static const unsigned long tempMessageInterval  = 100;
static const unsigned long msg81Interval        = 10;

static bool buttonActive = false;
static int lastRPM   = 0;
static int lastSpeed = 0;

// Temperature message array
static unsigned char tempMessage[8] = {0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};

// Forward declarations (internal)
static void prepareVINMessages();
static void handleButtonCommand(const String& command);
static void sendButtonMessage();
static void sendDefaultMessage();
static void sendVINMessages();
static void send3BMessages(unsigned long now);
static void send109Message();
static void sendABSTRACMessage();
static void sendRPMMessage(int rpmValue);
static void sendSpeedMessage(int speedValue);
static void sendTemperatureMessage();
static void sendTirePressureMessage();

void mustang_init(MCP_CAN* can) {
  canBus = can;
  prepareVINMessages();
}

void mustang_tick(unsigned long now) {
  // 0x81 button/default message (rate limited)
  if (now - last81MessageTime >= msg81Interval) {
    if (buttonActive && (now - buttonPressStartTime <= buttonHoldDuration)) {
      sendButtonMessage();
    } else {
      buttonActive = false;
      msgButton[1] = 0x00; // clear extra flags
      sendDefaultMessage();
    }
    last81MessageTime = now;
  }

  if (now - lastRPMTime >= rpmInterval) {
    sendRPMMessage(lastRPM);
    lastRPMTime = now;
  }

  if (now - lastSpeedTime >= speedInterval) {
    sendSpeedMessage(lastSpeed);
    lastSpeedTime = now;
  }

  if (now - lastTempMessageTime >= tempMessageInterval) {
    sendTemperatureMessage();
    lastTempMessageTime = now;
  }

  if (now - lastTirePressureTime >= tirePressureInterval) {
    sendTirePressureMessage();
    lastTirePressureTime = now;
  }

  if (now - lastVINMessageTime >= vinMessageInterval) {
    sendVINMessages();
    lastVINMessageTime = now;
  }

  if (now - last3BMessagesTime >= threeBMessagesInterval) {
    send3BMessages(now);
    last3BMessagesTime = now;
  }

  if (now - last109MessageTime >= message109Interval) {
    send109Message();
    last109MessageTime = now;
  }

  if (now - lastABSTRACMessageTime >= absTracInterval) {
    sendABSTRACMessage();
    lastABSTRACMessageTime = now;
  }
}

void mustang_handleCommand(String* tokens, int count) {
  if (count <= 0) return;

  String cmd = tokens[0];

  if (cmd == "RPM" && count >= 2) {
    lastRPM = tokens[1].toInt();
  } else if (cmd == "SPEED" && count >= 2) {
    lastSpeed = tokens[1].toInt();
  } else if (cmd == "TIRE" && count >= 3) {
    String tireName = tokens[1];
    float pressure = tokens[2].toFloat();
    bool found = false;
    for (int i = 0; i < 4; i++) {
      if (tireName == tireNames[i]) {
        tirePressuresPSI[i] = pressure;
        found = true;
        if (Serial) {
          Serial.print("[MUSTANG] Updated ");
          Serial.print(tireName);
          Serial.print(" to ");
          Serial.print(pressure);
          Serial.println(" PSI");
        }
        break;
      }
    }
    if (!found && Serial) {
      Serial.println("[MUSTANG] Tire Name Not Recognized");
    }
  } else if (cmd == "VIN" && count >= 2) {
    String newVIN = tokens[1];
    if (newVIN.length() != 17) {
      if (Serial) Serial.println("[MUSTANG] Invalid VIN Length");
      return;
    }
    newVIN.toCharArray(vin, 18);
    prepareVINMessages();
    if (Serial) {
      Serial.print("[MUSTANG] Updated VIN to: ");
      Serial.println(vin);
    }
  } else if (cmd == "TEMP" && count >= 3) {
    String type = tokens[1];
    int tempF = tokens[2].toInt();
    int tempC = (tempF - 32) * 5 / 9;
    int tempHex = tempC + 60;

    if (type == "COOLANT") {
      tempMessage[0] = tempHex;
    } else if (type == "OIL") {
      tempMessage[1] = tempHex;
    } else {
      if (Serial) Serial.println("[MUSTANG] Unknown TEMP type");
      return;
    }

    if (Serial) {
      Serial.print("[MUSTANG] Updated ");
      Serial.print(type);
      Serial.print(" temperature to ");
      Serial.print(tempF);
      Serial.println("°F");
    }
  } else if (cmd == "BLINKER" && count >= 2) {
    String which = tokens[1];
    if (which == "LEFT") {
      leftBlinkerActive = true;
      rightBlinkerActive = false;
    } else if (which == "RIGHT") {
      leftBlinkerActive = false;
      rightBlinkerActive = true;
    } else if (which == "BOTH") {
      leftBlinkerActive = true;
      rightBlinkerActive = true;
    } else if (which == "OFF") {
      leftBlinkerActive = false;
      rightBlinkerActive = false;
    }
  } else if (cmd == "HAZARDS") {
    leftBlinkerActive = !leftBlinkerActive;
    rightBlinkerActive = !rightBlinkerActive;
  } else if (cmd == "UP" || cmd == "DOWN" || cmd == "LEFT" ||
             cmd == "RIGHT" || cmd == "OK" || cmd == "SETTINGS") {
    handleButtonCommand(cmd);
  } else {
    if (Serial) {
      Serial.print("[MUSTANG] Unknown command: ");
      Serial.println(cmd);
    }
  }
}

// Internal helpers

static void prepareVINMessages() {
  memset(vinMessages, 0, sizeof(vinMessages));

  for (int i = 0; i < 17; i++) {
    int msgIndex  = i / 6;
    int byteIndex = (i % 6) + 2;

    vinMessages[msgIndex][0] = 0xC1;
    vinMessages[msgIndex][1] = (unsigned char)msgIndex;
    vinMessages[msgIndex][byteIndex] = (unsigned char)vin[i];
  }

  // Pad the last message with 0xFF if needed
  vinMessages[2][7] = 0xFF;
}

static void handleButtonCommand(const String& command) {
  if (command == "UP") {
    msgButton[0] = 0x08;
    msgButton[1] = 0x00;
  } else if (command == "DOWN") {
    msgButton[0] = 0x01;
    msgButton[1] = 0x00;
  } else if (command == "LEFT") {
    msgButton[0] = 0x02;
    msgButton[1] = 0x00;
  } else if (command == "RIGHT") {
    msgButton[0] = 0x04;
    msgButton[1] = 0x00;
  } else if (command == "OK") {
    msgButton[0] = 0x10;
    msgButton[1] = 0x00;
  } else if (command == "SETTINGS") {
    msgButton[0] = 0x46;
    msgButton[1] = 0x01;
  }

  buttonPressStartTime = millis();
  buttonActive = true;
}

static void sendButtonMessage() {
  if (!canBus) return;
  byte status = canBus->sendMsgBuf(0x81, 0, 8, msgButton);
  if (Serial && g_verboseSerial && canLog(millis())) {
    if (status == CAN_OK) Serial.println("[MUSTANG] Button Message Sent");
    else Serial.println("[MUSTANG] Error Sending Button Message");
  }
}

static void sendDefaultMessage() {
  if (!canBus) return;
  byte status = canBus->sendMsgBuf(0x81, 0, 8, msgDefault);
  if (Serial && g_verboseSerial && canLog(millis())) {
    if (status == CAN_OK) Serial.println("[MUSTANG] Default Message Sent");
    else Serial.println("[MUSTANG] Error Sending Default Message");
  }
}

static void sendVINMessages() {
  if (!canBus) return;

  for (int i = 0; i < 3; i++) {
    byte status = canBus->sendMsgBuf(0x40A, 0, 8, vinMessages[i]);
    if (Serial && g_verboseSerial && canLog(millis())) {
      if (status == CAN_OK) {
        Serial.print("[MUSTANG] VIN Message Part ");
        Serial.print(i);
        Serial.println(" Sent");
      } else {
        Serial.print("[MUSTANG] Error Sending VIN Message Part ");
        Serial.println(i);
      }
    }
  }
}

static void send3BMessages(unsigned long now) {
  if (!canBus) return;

  unsigned char msg[8] = {0x40, 0x48, 0xC0, 0x10, 0x10, 0x00, 0x00, 0x02};

  if (now - lastBlinkerToggle >= blinkerInterval) {
    blinkerState = !blinkerState;
    lastBlinkerToggle = now;
  }

  msg[4] = 0x10;
  msg[6] = 0x00;

  if (blinkerState) {
    if (rightBlinkerActive) msg[4] |= 0x08;
    if (leftBlinkerActive)  msg[6] |= 0x40;
  }

  canBus->sendMsgBuf(0x3B3, 0, 8, msg);
  canBus->sendMsgBuf(0x3B2, 0, 8, msg);
}

static void send109Message() {
  if (!canBus) return;

  unsigned char msg[8] = {0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x28};
  canBus->sendMsgBuf(0x109, 0, 8, msg);
}

static void sendABSTRACMessage() {
  if (!canBus) return;

  unsigned char msg[8] = {0x50, 0x00, 0xFE, 0x00, 0x01, 0x00, 0x00, 0x00};
  canBus->sendMsgBuf(0x416, 0, 8, msg);
}

static void sendRPMMessage(int rpmValue) {
  if (!canBus) return;

  int rpmHex = rpmValue / 2;

  unsigned char rpmMessage[8] = {0x00, 0x00, 0x00, 0x09, 0xC4, 0x00, 0x00, 0x00};
  rpmMessage[3] = (rpmHex >> 8) & 0xFF;
  rpmMessage[4] = rpmHex & 0xFF;

  byte status = canBus->sendMsgBuf(0x204, 0, 8, rpmMessage);
  if (Serial && g_verboseSerial && canLog(millis())) {
    if (status == CAN_OK) Serial.println("[MUSTANG] RPM Message Sent");
    else Serial.println("[MUSTANG] Error Sending RPM Message");
  }
}

static void sendSpeedMessage(int speedValue) {
  if (!canBus) return;

  int speedHex = speedValue * 159;

  unsigned char speedMessage[8] = {0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x25, 0x44};
  speedMessage[6] = (speedHex >> 8) & 0xFF;
  speedMessage[7] = speedHex & 0xFF;

  byte status = canBus->sendMsgBuf(0x202, 0, 8, speedMessage);
  if (Serial && g_verboseSerial && canLog(millis())) {
    if (status == CAN_OK) Serial.println("[MUSTANG] Speed Message Sent");
    else Serial.println("[MUSTANG] Error Sending Speed Message");
  }
}

static void sendTemperatureMessage() {
  if (!canBus) return;

  byte status = canBus->sendMsgBuf(0x156, 0, 8, tempMessage);
  if (Serial && g_verboseSerial && canLog(millis())) {
    if (status == CAN_OK) Serial.println("[MUSTANG] Temperature Message Sent");
    else Serial.println("[MUSTANG] Error Sending Temperature Message");
  }
}

static void sendTirePressureMessage() {
  if (!canBus) return;

  unsigned char tirePressureMessage[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  for (int i = 0; i < 4; i++) {
    int pressureKPA = (int)(tirePressuresPSI[i] * 6.895f + 0.5f); // PSI to kPa
    tirePressureMessage[2 * i + 1] = (unsigned char)(pressureKPA & 0xFF);
  }

  byte status = canBus->sendMsgBuf(0x3B5, 0, 8, tirePressureMessage);
  if (Serial && g_verboseSerial && canLog(millis())) {
    if (status == CAN_OK) Serial.println("[MUSTANG] Tire Pressure Message Sent");
    else Serial.println("[MUSTANG] Error Sending Tire Pressure Message");
  }
}

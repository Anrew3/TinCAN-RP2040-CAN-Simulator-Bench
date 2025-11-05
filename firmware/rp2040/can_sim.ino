#include <SPI.h>
#include "mcp_can.h"

#include "MustangModel.h"
#include "F150Model.h"
#include "Config.h"

#define CS_PIN 19

MCP_CAN CAN(CS_PIN);

enum ModelType { MODEL_MUSTANG, MODEL_F150 };
ModelType currentModel = MODEL_MUSTANG;

// Serial logging controls (default quiet)
bool g_verboseSerial = false;
unsigned long g_logMinIntervalMs = 0; // 0 = no rate limit
static unsigned long g_lastLogTime = 0;

bool canLog(unsigned long now) {
  if (!g_verboseSerial) return false;
  if (g_logMinIntervalMs == 0) return true;
  if (now - g_lastLogTime >= g_logMinIntervalMs) {
    g_lastLogTime = now;
    return true;
  }
  return false;
}

// -------- Custom CAN scheduler --------

#define MAX_CUSTOM_CAN 10

struct CustomCanEntry {
  bool used;
  unsigned long id;
  byte data[8];
  byte len;
  unsigned long intervalMs;
  unsigned long lastSent;
  long remaining;       // >0 = sends left, 0 = done, -1 = infinite
};

CustomCanEntry customCan[MAX_CUSTOM_CAN];

// Forward declarations
void split(const String& s, char delim, String* out, int& count, int maxParts);
bool getModelByName(const String& name, ModelType& out);
void setCurrentModel(const String& name);
void dispatchToModel(ModelType model, String* tokens, int count);
void handleSerialLine(const String& line);

// custom CAN helpers
unsigned long parseHex(const String& s);
void splitSpaces(const String& s, String* out, int& count, int maxParts);
void scheduleCustomCan(const String& frameDesc, long count, unsigned long intervalMs);
void updateCustomCan(unsigned long now);
void showCustomCan();
void cancelCustomCan(int slotIndex);

void setup() {
  Serial.begin(9600);

  if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK) {
    if (Serial) Serial.println("CAN Initialized Successfully!");
  } else {
    if (Serial) Serial.println("CAN Initialization Failed!");
    while (1); // hard fail if CAN chip isn't happy
  }

  CAN.setMode(MCP_NORMAL);

  // init both models with the same CAN object
  mustang_init(&CAN);
  f150_init(&CAN);

  // init custom CAN table
  for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
    customCan[i].used = false;
  }

  if (Serial) {
    Serial.println("CAN Simulator Ready");
    Serial.println("Default model: MUSTANG");
    Serial.println("Examples:");
    Serial.println("  MODEL:MUSTANG");
    Serial.println("  MODEL:F150:TEMP:COOLANT:190");
    Serial.println("  RPM:2000");
    Serial.println("Custom CAN examples:");
    Serial.println("  CAN:0x3AA 00 00 00 00 00 00 00 00:20:10");
    Serial.println("  CAN:3B3 40 48 C0 10 10 00 00 02:0:10  (0 = infinite)");
    Serial.println("  SHOWCAN");
    Serial.println("  CANCELCAN:0");
    Serial.println("Logging:");
    Serial.println("  VERBOSE:ON | VERBOSE:OFF");
    Serial.println("  LOGRATE:<ms>  (when verbose, min ms between lines)");
  }
}

void loop() {
  unsigned long now = millis();

  // Serial command handling
  if (Serial && Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      handleSerialLine(line);
    }
  }

  // Only the current model drives the bus (no ID collisions)
  if (currentModel == MODEL_MUSTANG) {
    mustang_tick(now);
  } else if (currentModel == MODEL_F150) {
    f150_tick(now);
  }

  // Update custom CAN jobs
  updateCustomCan(now);
}

void handleSerialLine(const String& line) {
  String parts[8];
  int count = 0;
  split(line, ':', parts, count, 8);
  if (count == 0) return;

  // --- Custom CAN commands (global, not per-model) ---
  // Logging controls
  if (parts[0] == "VERBOSE") {
    if (count >= 2) {
      String v = parts[1];
      v.toUpperCase();
      g_verboseSerial = (v == "ON" || v == "TRUE" || v == "1");
      if (Serial) {
        Serial.print("Verbose logging ");
        Serial.println(g_verboseSerial ? "ENABLED" : "DISABLED");
      }
    } else {
      if (Serial) Serial.println("Usage: VERBOSE:ON|OFF");
    }
    return;
  }

  if (parts[0] == "LOGRATE") {
    if (count >= 2) {
      unsigned long val = (unsigned long)parts[1].toInt();
      g_logMinIntervalMs = val;
      if (Serial) {
        Serial.print("Log rate limit set to ");
        Serial.print(g_logMinIntervalMs);
        Serial.println(" ms");
      }
    } else {
      if (Serial) Serial.println("Usage: LOGRATE:<ms>");
    }
    return;
  }

  // CAN:<id and data>:<count>:<intervalMs>
  // e.g. CAN:0x3AA 00 00 00 00 00 00 00 00:20:10
  if (parts[0] == "CAN") {
    if (count < 2) {
      if (Serial) Serial.println("Usage: CAN:<id data bytes>[:count][:intervalMs]");
      return;
    }

    String frameDesc = parts[1];
    frameDesc.trim();

    long repeat = 1;
    if (count >= 3) {
      repeat = parts[2].toInt();   // 0 = infinite
    }
    unsigned long intervalMs = 10;
    if (count >= 4) {
      intervalMs = (unsigned long)parts[3].toInt();
      if (intervalMs == 0) intervalMs = 1; // avoid 0ms hammer
    }

    scheduleCustomCan(frameDesc, repeat, intervalMs);
    return;
  }

  // SHOWCAN -> list all active custom CAN entries
  if (parts[0] == "SHOWCAN") {
    showCustomCan();
    return;
  }

  // CANCELCAN:<slotIndex>
  if (parts[0] == "CANCELCAN") {
    if (count < 2) {
      if (Serial) Serial.println("Usage: CANCELCAN:<slotIndex>");
      return;
    }
    int idx = parts[1].toInt();
    cancelCustomCan(idx);
    return;
  }

  // --- Model selection / dispatch ---

  // MODEL:<name>              -> change default model
  // MODEL:<name>:VERB:...     -> one-shot command for that model
  // VERB:...                  -> command for current default model
  if (parts[0] == "MODEL") {
    if (count == 2) {
      setCurrentModel(parts[1]);
      return;
    }
    if (count >= 3) {
      ModelType target;
      if (!getModelByName(parts[1], target)) {
        if (Serial) Serial.println("Unknown model");
        return;
      }
      dispatchToModel(target, &parts[2], count - 2);
      return;
    }
  }

  // No explicit model prefix -> current model
  dispatchToModel(currentModel, parts, count);
}

void split(const String& s, char delim, String* out, int& count, int maxParts) {
  count = 0;
  int start = 0;
  while (count < maxParts) {
    int idx = s.indexOf(delim, start);
    if (idx == -1) {
      out[count++] = s.substring(start);
      break;
    }
    out[count++] = s.substring(start, idx);
    start = idx + 1;
  }
}

bool getModelByName(const String& name, ModelType& out) {
  if (name.equalsIgnoreCase("MUSTANG")) {
    out = MODEL_MUSTANG;
    return true;
  }
  if (name.equalsIgnoreCase("F150") || name.equalsIgnoreCase("F-150")) {
    out = MODEL_F150;
    return true;
  }
  return false;
}

void setCurrentModel(const String& name) {
  ModelType m;
  if (getModelByName(name, m)) {
    currentModel = m;
    if (Serial) {
      Serial.print("Default model set to ");
      Serial.println(name);
    }
  } else {
    if (Serial) Serial.println("Unknown model");
  }
}

void dispatchToModel(ModelType model, String* tokens, int count) {
  if (model == MODEL_MUSTANG) {
    mustang_handleCommand(tokens, count);
  } else if (model == MODEL_F150) {
    f150_handleCommand(tokens, count);
  }
}

// -------- Custom CAN implementation --------

// Parse hex string like "3AA" or "0x3AA"
unsigned long parseHex(const String& sIn) {
  String s = sIn;
  s.trim();
  if (s.startsWith("0x") || s.startsWith("0X")) {
    s = s.substring(2);
  }
  unsigned long value = 0;
  for (int i = 0; i < s.length(); i++) {
    char c = s[i];
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
    else break; // stop at first non-hex
    value = (value << 4) | (unsigned long)digit;
  }
  return value;
}

// Split by spaces, collapsing multiple spaces
void splitSpaces(const String& s, String* out, int& count, int maxParts) {
  count = 0;
  int len = s.length();
  int i = 0;

  while (i < len && count < maxParts) {
    // skip leading spaces
    while (i < len && s[i] == ' ') i++;
    if (i >= len) break;
    int start = i;
    while (i < len && s[i] != ' ') i++;
    out[count++] = s.substring(start, i);
  }
}

// Schedule a custom CAN message job
// frameDesc: "0x3AA 00 00 00 00 00 00 00 00"
// count: >0 = number of times, 0 = infinite
// intervalMs: delay between sends in ms
void scheduleCustomCan(const String& frameDesc, long count, unsigned long intervalMs) {
  // Parse id + data bytes
  String tokens[9]; // id + up to 8 bytes
  int n = 0;
  splitSpaces(frameDesc, tokens, n, 9);
  if (n < 1) {
    if (Serial) Serial.println("CAN error: missing ID");
    return;
  }

  unsigned long id = parseHex(tokens[0]);
  if (id == 0 && !tokens[0].equalsIgnoreCase("0") && !tokens[0].equalsIgnoreCase("0x0")) {
    // probably bad parse
    if (Serial) {
      Serial.print("CAN warning: parsed ID as 0 from '");
      Serial.print(tokens[0]);
      Serial.println("'");
    }
  }

  byte data[8];
  byte len = (n - 1) > 8 ? 8 : (n - 1);
  for (int i = 0; i < len; i++) {
    data[i] = (byte)(parseHex(tokens[i + 1]) & 0xFF);
  }
  for (int i = len; i < 8; i++) {
    data[i] = 0x00;
  }

  if (intervalMs == 0) intervalMs = 1;

  // Find free slot
  int slot = -1;
  for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
    if (!customCan[i].used) {
      slot = i;
      break;
    }
  }
  if (slot == -1) {
    if (Serial) Serial.println("CAN error: no free custom slots");
    return;
  }

  customCan[slot].used       = true;
  customCan[slot].id         = id;
  customCan[slot].len        = len;
  for (int i = 0; i < 8; i++) customCan[slot].data[i] = data[i];
  customCan[slot].intervalMs = intervalMs;
  customCan[slot].lastSent   = millis() - intervalMs; // send ASAP on next tick
  customCan[slot].remaining  = (count > 0) ? count : -1; // 0 or negative -> infinite

  if (Serial) {
    Serial.print("Scheduled CAN slot ");
    Serial.print(slot);
    Serial.print(": ID=0x");
    Serial.print(customCan[slot].id, HEX);
    Serial.print(" DLC=");
    Serial.print(customCan[slot].len);
    Serial.print(" interval=");
    Serial.print(customCan[slot].intervalMs);
    Serial.print("ms remaining=");
    if (customCan[slot].remaining < 0) Serial.println("INF");
    else Serial.println(customCan[slot].remaining);
  }
}

// Called from loop()
void updateCustomCan(unsigned long now) {
  for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
    if (!customCan[i].used) continue;
    if (customCan[i].remaining == 0) {
      customCan[i].used = false;
      continue;
    }

    if (now - customCan[i].lastSent >= customCan[i].intervalMs) {
      byte status = CAN.sendMsgBuf(customCan[i].id, 0, customCan[i].len, customCan[i].data);
      customCan[i].lastSent = now;

      if (Serial && canLog(now)) {
        Serial.print("[CAN SLOT ");
        Serial.print(i);
        Serial.print("] Sent ID=0x");
        Serial.print(customCan[i].id, HEX);
        Serial.print(" status=");
        Serial.println(status == CAN_OK ? "OK" : "ERR");
      }

      if (customCan[i].remaining > 0) {
        customCan[i].remaining--;
        if (customCan[i].remaining == 0) {
          customCan[i].used = false;
          if (Serial && canLog(now)) {
            Serial.print("[CAN SLOT ");
            Serial.print(i);
            Serial.println("] Completed, removing");
          }
        }
      }
    }
  }
}

void showCustomCan() {
  if (Serial) {
    Serial.println("Active custom CAN slots:");
    bool any = false;
    for (int i = 0; i < MAX_CUSTOM_CAN; i++) {
      if (!customCan[i].used) continue;
      any = true;
      Serial.print("  [");
      Serial.print(i);
      Serial.print("] ID=0x");
      Serial.print(customCan[i].id, HEX);
      Serial.print(" DLC=");
      Serial.print(customCan[i].len);
      Serial.print(" interval=");
      Serial.print(customCan[i].intervalMs);
      Serial.print("ms remaining=");
      if (customCan[i].remaining < 0) Serial.print("INF");
      else Serial.print(customCan[i].remaining);
      Serial.print(" data=");
      for (int b = 0; b < customCan[i].len; b++) {
        if (b > 0) Serial.print(' ');
        if (customCan[i].data[b] < 0x10) Serial.print('0');
        Serial.print(customCan[i].data[b], HEX);
      }
      Serial.println();
    }
    if (!any) {
      Serial.println("  (none)");
    }
  }
}

void cancelCustomCan(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= MAX_CUSTOM_CAN) {
    if (Serial) Serial.println("CANCELCAN error: invalid slot index");
    return;
  }
  if (!customCan[slotIndex].used) {
    if (Serial) Serial.println("CANCELCAN: slot not in use");
    return;
  }
  customCan[slotIndex].used = false;
  if (Serial) {
    Serial.print("CANCELCAN: canceled slot ");
    Serial.println(slotIndex);
  }
}

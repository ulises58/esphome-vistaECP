/*
  VistaECP — ejemplo ESP32.
  Estado por el puerto serie. Comandos: help
*/

#include <VistaECP.h>

static vistaECPHome panel(17, 22, 21, 18, 48, 3, true, true, true);

static void printHelp() {
  Serial.println(F("help"));
  Serial.println(F("key <teclas>"));
  Serial.println(F("keyp <particion> <teclas>"));
  Serial.println(F("disarm [codigo] [particion]"));
  Serial.println(F("arm stay|away|night <particion>"));
}

static void onLine(const String &lineIn) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) return;

  if (line == "help" || line == "?") {
    printHelp();
    return;
  }

  if (line.startsWith("keyp ")) {
    int sp = line.indexOf(' ', 5);
    if (sp < 0) {
      Serial.println(F("uso: keyp <particion> <teclas>"));
      return;
    }
    int part = line.substring(5, sp).toInt();
    panel.alarm_keypress_partition(std::string(line.substring(sp + 1).c_str()), part);
    return;
  }

  if (line.startsWith("key ")) {
    panel.alarm_keypress(std::string(line.substring(4).c_str()));
    return;
  }

  if (line.startsWith("disarm")) {
    String rest = line.substring(6);
    rest.trim();
    String code = "1234";
    int part = 1;
    if (rest.length()) {
      int sp = rest.indexOf(' ');
      if (sp < 0) code = rest;
      else {
        code = rest.substring(0, sp);
        part = rest.substring(sp + 1).toInt();
      }
    }
    panel.alarm_disarm(std::string(code.c_str()), part);
    return;
  }

  if (line.startsWith("arm ")) {
    String rest = line.substring(4);
    rest.trim();
    int sp = rest.indexOf(' ');
    if (sp < 0) {
      Serial.println(F("uso: arm stay|away|night <particion>"));
      return;
    }
    String mode = rest.substring(0, sp);
    int part = rest.substring(sp + 1).toInt();
    if (mode == "stay") panel.alarm_arm_home(part);
    else if (mode == "away") panel.alarm_arm_away(part);
    else if (mode == "night") panel.alarm_arm_night(part);
    else Serial.println(F("modo: stay, away o night"));
    return;
  }

  Serial.println(F("comando desconocido"));
}

static char lineBuf[120];
static size_t lineLen;

void setup() {
  Serial.begin(115200);
  delay(300);

  panel.set_accessCode("1234");
  panel.set_debug(1);
  panel.set_lrrSupervisor(false);

  panel.setBinarySensorCallback([](const std::string &id, uint8_t partition, bool value) {
    Serial.printf("[bin] %s_%u = %s\n", id.c_str(), (unsigned)partition, value ? "true" : "false");
  });
  panel.setTextSensorCallback([](const std::string &id, uint8_t partition, std::string *text) {
    Serial.printf("[txt] %s_%u = %s\n", id.c_str(), (unsigned)partition, text ? text->c_str() : "");
  });

  panel.begin();
  Serial.println(F("VistaECP listo. Escribe help."));
}

// Cada minuto, si cambiaron, los contadores del serial por software. Si suben con la red
// cargada, hay interrupciones del bus que llegan tarde.
static void printBusStats() {
  static uint32_t last = 0;
  static uint32_t seen[4] = {0, 0, 0, 0};
  if (millis() - last < 60000) return;
  last = millis();
  uint32_t now[4] = {vista.panelChksumFails, vista.monitorChksumFails, vista.rxOverflows, vista.monitorOverflows};
  if (memcmp(now, seen, sizeof(now)) == 0) return;
  memcpy(seen, now, sizeof(now));
  Serial.printf("[bus] checksum panel=%lu monitor=%lu, overflow rx=%lu monitor=%lu\n",
                (unsigned long)now[0], (unsigned long)now[1], (unsigned long)now[2], (unsigned long)now[3]);
}

void loop() {
  panel.loop();
  printBusStats();

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      onLine(String(lineBuf));
      lineLen = 0;
    } else if (lineLen + 1 < sizeof(lineBuf)) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;
    }
  }
}

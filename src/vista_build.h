#pragma once
/**
 * Activa el modo Arduino puro de vistaalarm (misma lógica que ESPHome, sin esphome::*).
 * Debe incluirse antes de vistaalarm.h en cualquier .cpp que compile el panel.
 */
#define ARDUINO_MQTT 1

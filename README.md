# VistaECP

Librería Arduino para hablar con un panel Honeywell / Ademco Vista por el bus ECP, solo en ESP32.

No es un componente de ESPHome ni de Home Assistant. El programa que la usa decide qué hacer con el estado.

## Qué se quedó

El protocolo ECP, el mismo de [Dilbert66/esphome-vistaECP](https://github.com/Dilbert66/esphome-vistaECP) y de `esphome-components`. Licencia LGPL-2.1: ver `COPYING.LESSER`.

Sigue pudiendo:

- Anunciarse en el panel como teclado (`*190` a `*196`). La dirección por defecto es 17.
- Mandar teclas, armar en casa, ausente o noche, desarmar, fuego y pánico.
- Leer zonas, particiones y el texto de la pantalla por callback.
- Emular expansor de zonas, radio y radio de largo alcance. El ejemplo corto no los usa.

## Qué se actualizó

Pasó de componente ESPHome a librería Arduino. Se incluye con `VistaECP.h`. El modo de compilación queda fijo en Arduino (`ARDUINO_MQTT`): las ramas de ESPHome no entran al programa.

Los pines por defecto siguen siendo los del ejemplo original: recepción 22, transmisión 21, monitor 18.

## Qué se quitó

ESP8266, RP2040, los YAML de ESPHome, la tarjeta de Home Assistant y el ejemplo MQTT de esas plataformas. El programa de arriba decide cómo publicar el estado.

## Núcleo Arduino-ESP32 3.x

Está escrita con las llamadas que el núcleo 3.x sigue teniendo: interrupciones de GPIO, `esp_timer` y FreeRTOS. No usa el temporizador ni el LEDC con la forma vieja del núcleo 2.x.

No está grabada ni probada todavía sobre 3.3.12. El lab de Vista se compilaba con 2.0.17. Al pasarla al núcleo 3.x hay que compilar el ejemplo antes de darla por buena.

## Pines por defecto

Son los del ejemplo original.

| Señal | GPIO |
|---|---|
| Amarillo, recepción | 22 |
| Verde, transmisión | 21 |
| Verde, monitor | 18 |

El constructor de `vistaECPHome` acepta otros pines.

## Uso

Hay que programar en el panel una dirección de teclado libre (`*190` a `*196`) y pasarla al constructor. La dirección por defecto es 17.

```cpp
#include <VistaECP.h>

vistaECPHome panel(17, 22, 21, 18, 48, 3, true, true, true);

void setup() {
  panel.set_accessCode("1234");
  panel.begin();
}

void loop() {
  panel.loop();
}
```

El ejemplo `examples/VistaECP` imprime el estado por el puerto serie y acepta comandos (`help`).

## Qué no incluye

ESP8266, RP2040, YAML de ESPHome, tarjeta de Home Assistant y topics MQTT. Esas capas viven en el programa de arriba, igual que en la librería DSC.

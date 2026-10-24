# Honeywell/Ademco Vista ECP library

This fork keeps Dilbert66's ECP library and ships it as an Arduino library for the ESP32. The sources are in `src/`. The example sketch is `examples/VistaECP`. ESPHome, the Home Assistant card, and the ESP8266 build are not in this fork.

## Table of contents

- [Honeywell/Ademco Vista ECP library](#honeywellademco-vista-ecp-library)
  * [Table of contents](#table-of-contents)
  * [About the project](#about-the-project)
  * [Features](#features)
  * [Changes in this fork](#changes-in-this-fork)
- [How to install](#how-to-install)
  * [⚠️ Warning!!! ⚠️](#⚠️-warning-⚠️)
  * [Prerequisites](#prerequisites)
    + [Identify Vista panel model](#identify-vista-panel-model)
  * [Project Structure and Wiring](#project-structure-and-wiring)
  * [Connecting everything](#connecting-everything)
- [FAQ](#faq)
  * [Supported Models](#supported-models)
- [Report issue](#report-issue)
- [References](#references)
- [Author & Contributors](#author---contributors)

## About the project

This project allows you to monitor and control your old Vista alarm panel from an ESP32.<br>

If you like this project and wish to supplement my coffee intake, please click the button below to donate! Thank you!

[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/Dilbert66)

Got a DSC PowerSeries panel? Have a look at the <a href="https://github.com/Dilbert66/esphome-dsckeybus">following project</a>.<br>

This is an implementation of an ESPHOME custom component and ESP Library to interface directly to a Safewatch/Honeywell/Ademco Vista 15/20 alarm system using the ECP interface and very inexpensive ESP8266/ESP32 modules .  The ECP library code is based on the arduino source code from Mark Kimsal's repository located at  https://github.com/TANC-security/keypad-firmware.  It has been  completely rewritten as a class and adapted to work on the ESP8266/ESP32 platform using interrupt driven communications and pulse timing. A custom modified version of Peter Lerup's ESPsoftwareserial library (https://github.com/plerup/espsoftwareserial) was also used for the serial communications to work more efficiently within the tight timing of the ESP8266 interrupt window. 

From documented info, it seems that some panels send an F2 command with extra system details but the panel I have here (Vista 20P version 3.xx ADT version) does not.  Only the F7 is available for zone and system status in my case but this is good enough for this purpose. 

As far as writing on the bus and the request to send pulsing sequence, most documentation only discusses keypad traffic and this only uses the the 3rd pulse.  In actuality the pulses are used as noted below depending on the device type requesting to send:
```
Panel pulse 1. Addresses 1-7, expander board (07), etc
Panel pulse 2. Addresses 8-15 - zone expanders, relay modules
Panel pulse 3. Addresses 16-23 - keypads
```
For example, a zone expander that has the address 07, will send it's address on the first pulse only and will send nothing for the 2nd and 3rd pulse.  A keypad with address 16, will send a 1 bit pulse for pulse1 and pulse2 and then it's encoded address on pulse 3. This info was determined from analysis using a zone expander board and Pulseview to monitor the bus. 

The library class is `vistaECPHome` in `src/`. Call it from your own sketch. See `examples/VistaECP`.

For multi partition support, it is important that you first program new keypad addresses assigned to each partiton you want to support using programs `*190` - `*196` on the vista panel.  Once done, pass those addresses to the keypad for each partition.  For unused partitions, leave the address at 0. 

##### Notes: 

* If you use the zone expanders and/or LRR functions, you might need to clear CHECK messages for the LRR and expanded zones from the panel on boot or restart by entering your access code followed by 1 twice. eg 12341 12341 where 1234 is your access code.

**Read more in the next wiki paragraphs for a detailed step by step guide.**

## Changes in this fork

Everything below is on top of Dilbert66's library. The bus is still read and written with the software serial (`ECPSoftwareSerial`), as in the original.

### Packaging

- Arduino library for the ESP32 only. ESPHome, the Home Assistant card, the MQTT examples and the ESP8266 build were removed.
- Pins, inversions, zones and partitions are passed to the `vistaECPHome` constructor from your sketch. The defaults (`RX_PIN 22`, `TX_PIN 21`, `MONITOR_PIN 18`) are only the original project's example pins; check them against your board.

### Fixes

- **Armed sensors stayed on after disarming.** When a disarm left the panel not ready, the armed sensors (`arm`, `arma`, `arms`, `armn`, `armi`) are only published on frames with `systemFlag`, but `previousLightState` was updated on every frame. The next system frame then saw no change and the sensors stayed `true`. They are now compared against the value last published (`publishedArmed` in `partitionStateType`). Same fix as [pletch/esphome-vistaECP-idf](https://github.com/pletch/esphome-vistaECP-idf) commit `eab81b9` (issue #5 there).
- **Zone faults during programming mode.** The keypad text in installer mode was parsed as faulted zones. The zone fault check now skips `programMode`. Same fix as pletch commit `affb02a`.
- **Key queue overflow.** `write()` and `writeDirect()` did not check if the 50-entry key queue was full. With more than 49 pending keys the index wrapped onto the read index, the queue looked empty and every pending key was lost. A key that does not fit is now dropped and the queued ones are kept.

### Software serial reliability

The library times each edge of the yellow (and green) line from a GPIO interrupt. A byte is lost or corrupted when that interrupt runs late.

- **The interrupt path no longer calls code in flash.** `gpio_get_level()` and `gpio_set_level()` live in flash in ESP-IDF unless `CONFIG_GPIO_CTRL_FUNC_IN_IRAM` is set (it is not by default), and Arduino's `micros()` is not in IRAM either (`CONFIG_ARDUINO_ISR_IRAM=n`). The interrupt now reads and writes the pin registers directly (`gpio_ll_get_level`, `gpio_ll_set_level`) and uses `esp_timer_get_time()`, which is in IRAM.
- **The GPIO interrupt service is installed with `ESP_INTR_FLAG_IRAM`**, so the bus interrupt keeps running while the flash is being written (for example, saving settings to NVS). If your sketch calls `attachInterrupt()` before `panel.begin()`, Arduino installs the service first without that flag; the library then keeps working, but the bus interrupt pauses during flash writes. Call `panel.begin()` first.
- **Error counters**, public members of the global `vista` object. They only go up:

  | Counter | Counts |
  |---------|--------|
  | `vista.panelChksumFails` | Panel (yellow) messages with a bad checksum |
  | `vista.monitorChksumFails` | Green-line (monitor) messages with a bad checksum |
  | `vista.rxOverflows` | Times the yellow edge buffer filled up (bits lost) |
  | `vista.monitorOverflows` | Same for the green line |

  The example sketch prints them every minute when they change. Load the network (MQTT, web page, Wi-Fi traffic) and watch them: if they stay at zero or close to it, the software serial is keeping up.

- **Keep the network off the bus core.** The library runs its task and its interrupts on core 1 (`ASYNC_CORE` in `vistaalarm.h`). Wi-Fi and the TCP stack already run on core 0. If your sketch does slow network work, run it in a task on core 0 so core 1 stays free for the bus.

If the counters still climb after this, the next step is a hardware UART for the yellow line, as pletch's fork does. That is a rewrite of the receive path (address pulses and 2400 baud segments have to be detected differently), not a patch.

## Features


* Full zone expander emulation (4219/4229) which will give you  an additional 8 zones to the system per emulated expander plus associated relay outputs. Currently the library will provide emulation for 2 boards for a total of 16 additionals zones. You can even use free pins on the chip as triggers for those zones as well. 

* Full independent partition support. The firmware allows control and view status of all 3 partitions.

* Relay module emulation. (4204). The system can support 4 module addresses for a total of 16 relay channels. 

* Long Range Radio (LRR) emulation (or monitoring) statuses for more detailed status messages

* Zone status - Open, Alarmed, Closed and Bypass with named zones

* Arm, disarm or send any sequence of commands to the panel for any partition

* Status indicators - fire, alarm, trouble, armed stay, armed away, instant armed, armed night,  ready, AC status, bypass status, chime status,battery status, check status, zone and relay channel status field of individual partitions.


* Optional ability to monitor other devices on the bus such as keypads, other expanders, relay boards, RF devices, etc. This requires the #define MONITORTX to be uncommented in vista.h as well as the addition of two resistors (R4 and R5) to the circuit as shown in the schematic.   This adds another serial interrupt routine that captures and decodes all data on the green tx line.  If enabled this data will be used to update zone statuses for external modules.


The class provides these calls: 
```
alarm_disarm: Disarms the alarm with the user code provided, or the code specified in the configuration.
alarm_arm_home: Arms the alarm in home mode.
alarm_arm_away: Arms the alarm in away mode.
alarm_arm_night: Arms the alarm in night mode (no entry delay).
alarm_trigger_panic: Trigger a panic alarm.
alarm_trigger_fire: Trigger a fire alarm.
alarm_keypress: Sends a string of characters to the default partition. (Controlled by the keypad1 address)
alarm_keypress_partition: Sends a string of characters to any specified partition. (1, 2 or 3)
```

# How to install

## ⚠️ Warning!!! ⚠️

⚠️ **When opening the panel and dealing with the alarm panel itself throughout the reading of this wiki be careful as AC current is used to power the panel, make sure to switch it off before touching the terminals or any other part of the circuits.** ⚠️

##  Prerequisites

In order to install the project you need to:
- [Identify your Vista alarm panel model](#identify-vista-panel-model)
- [Get the needed components for the project and wire them](#project-structure-and-wiring)
- Flash `examples/VistaECP` (or your own sketch) to an ESP32
- [Connect everything](#connecting-everything)


### Identify Vista panel model
Open the panel and check the interior side of the panel's door, there should be the wiring diagram indicating the Vista model right below (usually on bottom right or center). <br>

*Example: Vista-20P, Vista-20SE, Vista-20SEa, Vista-25IT, ...*<br>

If the wiring diagram is not present, check the circuit board. You should be able to see what model it is by looking either at the stamps/labels on the circuit board or by looking at the model on the CPU. The model here sometimes is in form of "SAVS**20P**5" or similar.<br> It is recommended to check also the circuit board even if you found the model on the wiring diagram, just to be sure that they match.

See image below, red arrows indicates where it could be.

![Vista Model Circuit Board IMG](readme_material/vista_panel_door_3d.png)

Check [Supported Models](#supported-models) to get an overview of compatible models and continue to next section. 

If your device is not present in the list you could still try using one of the code versions available in the repository, **but the functioning is not guaranteed**.

Got a DSC PowerSeries panel? Have a look at the <a href="https://github.com/Dilbert66/esphome-dsckeybus">following project</a>.

##  Project Structure and Wiring

After you identified the vista panel's model then you can start preparing for the hardware part and the first HW part you need to get is an ESP device. <br>

If you're starting from scratch, probably it's better to start with an ESP32 as it's more powerful and versatile, otherwise make your evaluation based on the code you select to install on the ESP device, but due to further analyses the ESP32 is the most suitable device for this project. Let's analyze the project structure.<br>

This fork targets an ESP32. Make sure you have the GPIO numbers pin mapping, sometimes the pins don't match the ones reported in the pcb's silkscreen.<br>

Now that you identified the panel and got an ESP32, it's time to choose a wiring configuration to the panel.

The available ones in the repo are:
1. Non-isolated simple version (**preferred**)

![ecpinterface](readme_material/master_non_is_simple.png)

Notes: This is the recommended version. It provides the best signal output with very minimal impact on the ECP bus and using minimal components.



2. Alternative Non-isolated simple version 

![image](https://github.com/Dilbert66/esphome-vistaECP/assets/7193213/9d187e45-2d16-47bf-8e7b-fd8435b9593d)



3. Ground isolated version

![ecpinterface-isolated](readme_material/master_gnd_isolated.png)

Notes: This version is the least recommended as it will load the ECP bus to a certain extent. It also does not provide the best signal output. This is all dependant on the bus it is connected to and the quality of the optocouplers used. Your mileage may vary.<br><br>
* Optocouplers should have a minimum CTR of 50. Recommendations are the 4N35 or TLP521. You can vary the resistor values for the simple version but keep the ratio similar for the voltage dividers R2/R3 and R4/R5. R1 should not be set below 150 ohm or 100ohm when using an ESP32. Resistor values are chosen to minimize load on ECP bus while still providing full output signals on the optocouplers. <br>
* My goal was to keep the design as simple as possible without causing any bus load or interference with maximum signal fidelity.  Since the transmit circuit required high side switching I opted to use an optocoupler since I had a few on hand and it simplified the amount of components needed but proved to have it's own issues as far as CTR requirements.  For those that would prefer not using optocouplers due to availability or other reasons, i've provided a version using transistors for the transmit circuit instead. <br>

The first two are the same, but the alternative versions use transistors instead of optocouplers.
The third one instead is the ground isolated version, meaning that the ground of the panel (black wire) and the ground of the ESP device are separated. There is no particular advantage of using it.<br> // TO BE CHECKED
Read the notes on the drawings for more details.

Get the other components: 
- breadboard or PCB (if you prefer soldering)
- jumper cables
- 4-inputs screw terminal for the cables from the panel to the ESP (opt)
- resistors (you can use multiple resistors in series or parallel if you can't find kits with the exact value of the diagrams, i.e. 4.7KOhm can be obtained by using two resistors in parallel -> 100kohm + 5.1kohm =~ 4.8KOhm)
- optocouplers / transistors
- power supply or USB for the ESP device (highly recommended) or a 12V to 5V/3.3V regulator (to take the power from the panel)
- juction box (optional, to protect the ESP device if installed outside the panel)

Thanks to <a href="https://community.home-assistant.io/u/howardchen3">`@howardchen3`</a> there is a PCB layout of the ground-isolated version available at <a href="https://oshwlab.com/boobeechen/vistaesphome_copy">oshwlab</a> that could be used to send to a manufacturer. Read more on the site.


Wire the breadboard/PCB part according the wiring diagram you selected, but for the moment keep the ESP unplugged from both panel and breadboard/pcb since we're going to connect the ESP to the PC and it's not handy to have all the wires around. 

You should be in a situation like this now:
![ecp_breadboard](readme_material/esp_breadboard.png)

Esp is ready to be connected to the PCB/breadboard and the latter is ready to be connected at the panel.<br>
⚠️ **DO NOT CONNECT THEM YET** ⚠️

**Very important step**: Double check that the connections are ok (add some black tape to keep the loose connections in place if you opted for a breadboard). It is difficult to debug for HW problems once phisically attached to the panel or near to it as often the panels are not in comfy locations.

## Connecting everything 

⚠️ **Before continuing read [Warning notice](#⚠️-warning-⚠️)** ⚠️

Let's connect the ESP device to the breadboard or PCB and finally let's connect the breadboard/PCB to the panel. Depending on the configuration you selected, you need to connect the breadboard/PCB to the same keypad terminals of your existing physical keyboard **OR**, if you prefer, on the keypad terminals of another partition of your panel (in case of multi-partition panel).

**Make sure to keep the ESP device further from any RF receiver as the components and the ESP itself could cause interference.**

Power up the ESP device and open the serial monitor at 115200. The example sketch prints a ready line and accepts `help`.

**Optional steps**

Cover the ESP device with a junction box or similar just to make a cover. Remember to keep at least two holes for some air venting.

Final result:

![everything_connected](readme_material/everything_connected.png)

Note: Even the circuitry can be placed outside if you want. The wires from circuitry to the ESP are just for demo purposes, not related at all to the wiring diagrams. The power wire depends on the setup you chose, but it's recommended to have an external power supply.

# FAQ

1. How do I use the Trace Analyzer for deeper investigation?

Connect the pulseview channel D0 to the yellow line input on the D1 ESP input pin and the pulseview channel D1 to the green line monitor input D5 pin on ESP and then finally ground to the common ground on the ESP side. You don't need to monitor VCC or the ESP D2 output since that will also be seen on the green line monitor input at D5. Make sure you don't connect the channel inputs to any voltage higher than around 5 volts.

You should not be sharing ground between the panel and the esp if you used the optocoupler isolated schematic to build your circuit. if you did, it won't hurt anything, it's just not needed.

The setup file is really just the decoder setup so you that you can actually see the data being sent. It's attached below.
Just unzip it and load it into PulseView software by selecting "Restore session setup" from the menu. Ensure you connect your device first before running PulseView so that it picks it up on start up. (it will show up as a Saelea 8 channel). Once it starts your session, you will see 8 inputs. Once you restore the saved session setup, you will see 3 channels and 2 decoders. One for the panel (yellow line) and one for the keypad (green line). You'll need to run the capture twice to actually capture data. Seems like the first time after loading a session, it does not see the channels but the second run is fine.

<a href="https://github.com/Dilbert66/esphome-vistaECP/files/7150101/vistaecp2.zip">vistaecp2.zip</a>

Once you have about 1 minute or so of data you can then click the save button to save the session. It will create an *.sr file.
zip that and attach it to the issue.

2. What is LRR?

LRR = Long Range Radio. IP or GSM interface and monitored by a central monitoring station.

3. What is an address?

A zone number. Zone number = Address. //TO BE CHECKED

4. How do I setup the expanders?

// TO BE CHECKED

5. What is the access code?

Your 4 digit secret number to arm/disarm the system. e.g. 1234

6. What is quickArm?

It will the hashtag instead of your access code for the arm command. E.g. it will send #3 instead of 12343 to enable "stay" mode.

7. What is RFMsg?

It is a string variable that will be populated with the messages from your RF devices, if any. Read more in the yaml configuration section (rfSerialLookup).<br>
In general, the RF message will contain the serial number and a status byte (with the sensor data).<br>
```
  bit 1 - ?
  bit 2 - Battery (0=Normal, 1=LowBat)
  bit 3 - Heartbeat (Sent every 60-90min) (1 if sending heartbeat)
  bit 4 - ?
  bit 5 - Loop 3 (0=Closed, 1=Open)
  bit 6 - Loop 2 (0=Closed, 1=Open)
  bit 7 - Loop 4 (0=Closed, 1=Open)
  bit 8 - Loop 1 (0=Closed, 1=Open)
```
// TO BE CHECKED

8. How do I open an issue?

See [How to](#report-issue) section.


9. What are the possible states of the text_sensors?

O, C, A, T, B:
```
O = Open / Fault
C = Closed
A = Alarm
T = Trouble
B = Bypass
```

## Supported Models

As of today:

|  **Model** |  **Source Code**  |
|:----------:|:-----------------:|
| Vista 20P  | vistaEcpInterface |
| Vista 20SE | vistaEcpInterface |
| Vista 25IT | vistaEcpInterface |
| Vista120 | vistaEcpInterface - // TO BE CHECKED |
| Vista128bpt/250bpt | vistaEcpInterface - // TO BE CHECKED |
| | |
| | |
| | |
| | |


# Report issue

First: search for your issue into the closed and open issues in the repo, if you don't find anything, open a new issue specifying:<br>
- Panel Model
- Used ESP model
- Used wiring diagram
- Sketch configuration (the necessary parts)
- Issue
- Installed sensors (if relevant)
- What you've tried
- Logs

# References

## References 

You can checkout the links below for further reading and other implementation examples. Some portions of the code in the repositories below was used in creating the library.
* https://github.com/TANC-security/keypad-firmware
* https://github.com/cweemin/espAdemcoECP
* https://github.com/TomVickers/Arduino2keypad/

This project is licensed under the `Lesser General Public License` version `2.1`, or (at your option) any later version as per it's use of other libraries and code. Please see `COPYING.LESSER` for more information.

# Author & Contributors

- @Dilbert66 (Author)
- @appleguru (Contributor)
- @nitej (Contributor)
- @rcmurphy (Contributor)
- @jdev-homeauto (Contributor)
- @lorenzodeveloper (Contributor)


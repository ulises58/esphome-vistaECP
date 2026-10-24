// for documentation see project at https://github.com/Dilbert66/esphome-vistaecp
#include <Arduino.h>
#include <algorithm>
#include <vector>
#include <time.h>
#include <esp_log.h>
#include "vistaalarm.h"

static const char *TAG = "vista_alarm";
Vista vista;
vistaECPHome *alarmPanelPtr = nullptr;


#include <esp_chip_info.h>
#include <esp_task_wdt.h>

#define KP_ADDR 17 // only used as a default if not set in the yaml
#define MAX_ZONES 48
#define MAX_PARTITIONS 3
#define DEFAULTPARTITION 1

// default pins to use for serial comms to the panel
// The pinouts below are only examples. You can choose any other gpio pin that is available and not needed for boot.
// These have proven to work fine.
#define RX_PIN 22
#define TX_PIN 21
#define MONITOR_PIN 18


    void vistaECPHome::stop()
    {
       if (xHandle != nullptr)
         vTaskSuspend(xHandle);
      vista.stop();
    }

    vistaECPHome::vistaECPHome(char kpaddr, int receivePin, int transmitPin, int monitorTxPin, int maxzones, int maxpartitions, bool invertrx, bool inverttx, bool invertmon, uint8_t inputrx, uint8_t inputmon) : _keypadAddr1(kpaddr),
                                                                                                                                                                                                                   _rxPin(receivePin),
                                                                                                                                                                                                                   _txPin(transmitPin),
                                                                                                                                                                                                                   _monitorPin(monitorTxPin),
                                                                                                                                                                                                                   _maxZones(maxzones),
                                                                                                                                                                                                                   _maxPartitions(maxpartitions),
                                                                                                                                                                                                                   _invertRx(invertrx),
                                                                                                                                                                                                                   _invertTx(inverttx),
                                                                                                                                                                                                                   _invertMon(invertmon),
                                                                                                                                                                                                                   _inputRx(inputrx),
                                                                                                                                                                                                                   _inputMon(inputmon)
    {
      _partitionKeypads = new char[_maxPartitions + 1];
      _partitions = new uint8_t[_maxPartitions];
      partitionStates = new partitionStateType[_maxPartitions];
      fireStatus = new alarmStatusType;
      panicStatus = new alarmStatusType;
      alarmStatus = new alarmStatusType;
      alarmPanelPtr = this;
    }

vistaECPHome::~vistaECPHome()
{   
      delete[] _partitionKeypads;
      delete[] _partitions;
      delete[] partitionStates;
      delete alarmStatus;
      delete fireStatus;
      delete panicStatus;

}

    void vistaECPHome::zoneStatusUpdate(sensorObjType *zt)
    {
      if (!zt->zone) return;

      std::string msg, zs1, lb;
      zs1 = zt->check ? "T" : zt->open ? "O"
                                       : "C";
      msg = zt->bypass ? "B" : zt->alarm ? "A"
                                         : "";
      lb = zt->lowbat || zt->rflowbat ? "L" : "";
      msg.append(zs1).append(lb);
      publishZoneStatus(zt, msg.c_str());

      if (zt->zone <= _maxZones) 
        publishZoneStatus(zt, zt->open || zt->check);
      else
        publishZoneStatus(zt, zt->check || zt->open || zt->alarm || zt->trouble);
    }


    void vistaECPHome::publishBinaryState(const std::string &cstr, uint8_t num, bool open)
    {
      if (binarySensorCallback)
        binarySensorCallback(cstr, num, open);
    }

    void vistaECPHome::publishTextState(const std::string &cstr, uint8_t num, std::string *text)
    {
      if (textSensorCallback)
        textSensorCallback(cstr, num, text);
    }


    vistaECPHome::sensorObjType *vistaECPHome::getSensorObj(const char *id_type)
    {
 
      auto it = std::find_if(extZones.begin(), extZones.end(), [id_type](sensorObjType &f)
                             { return strcmp(f.id_type,id_type) == 0; });
      if (it != extZones.end()) 
        return &(*it);
      else {
        return nullptr;
      }

    }


    void vistaECPHome::createZone(uint16_t z, uint8_t p)
    {

      sensorObjType *zt = getZone(z);
      if (zt->zone == z)
        return;

      sensorObjType n = sensorObjType_INIT;
      n.zone = z;
      n.active = true;
      n.partition = p;
      getRFSerial(&n);

      extZones.push_back(n);
      ESP_LOGD(TAG, "added zone %d", extZones.back().zone);
      publishZoneStatus(&n, "C");
      publishZoneStatus(&n, false);
    }


    void vistaECPHome::getZoneName(uint16_t zone, std::string &out,  bool append)
    {
     out = std::to_string(zone);
     

    }

    vistaECPHome::sensorObjType *vistaECPHome::getZone(uint16_t z)
    {

      auto it = std::find_if(extZones.begin(), extZones.end(), [&z](sensorObjType &f)
                             { return f.zone == z; });
      if (it != extZones.end())
        return &(*it);
      return &sensorObjType_INIT;
    }

    vistaECPHome::sensorObjType *vistaECPHome::getZoneFromSerial(uint32_t serialCode)
    {
      auto it = std::find_if(extZones.begin(), extZones.end(), [serialCode](sensorObjType &f)
                             { return f.serial == serialCode; });
      if (it != extZones.end())
      {
        return &(*it);
      }
      return &sensorObjType_INIT;
    }

    void vistaECPHome::getRFSerial(sensorObjType *zt)
    {

      if (_rfSerialLookup != NULL && *_rfSerialLookup)
      {
        std::string s = _rfSerialLookup;

        size_t pos, pos1, pos2;
        s.append(",");
        while ((pos = s.find(',')) != std::string::npos)
        {
          std::string token, token1, token2, token3;
          token = s.substr(0, pos);
          pos1 = token.find(':');
          pos2 = token.find(':', pos1 + 1);
          token1 = token.substr(0, pos1); // serial
          if (pos2 != std::string::npos)
          {
            token2 = token.substr(pos1 + 1, pos2 - pos1 - 1); // loop
            token3 = token.substr(pos2 + 1);                  // zone
          }
          if (token1 != "" && token2 != "" && token3 != "")
          {
            uint8_t zone = toInt(token3, 10);
            uint8_t loop = toInt(token2, 10);
            uint32_t rfserial = toInt(token1, 10);
            if (rfserial > 0 && zt->zone == zone)
            {
              zt->loopmask=getLoopMask(loop,RF_TYPE);
              zt->serial = rfserial;
              zt->type=RF_TYPE;
              return;
            }
          }
          s.erase(0, pos + 1); /* erase() function store the current positon and move to next token. */
        }
      }
    }


    void vistaECPHome::processAuiQueue()
    {
      if (auiCmd.state != rsidle && (millis() - auiCmd.time) > 5000)
      { // reset auicmd state if no f2 response after 5 seconds
        ESP_LOGD("TAG", "Setting auicmd state to idle");
        auiCmd.state = rsidle;
        auiCmd.pending = false;
      }
      if (auiQueue.size() > 0 && auiCmd.state == rsidle)
      {
        auiCmd = auiQueue.front();
        auiQueue.pop();
        switch (auiCmd.state)
        {
        case rsdate:
          sendAuiTime();
          break;
        case rsopenzones:
          sendZoneRequest();
          break;
        default:
          break;
        }
      }
    }

    void vistaECPHome::begin()
    {
      ESP_LOGD(TAG, "Start setup: Free heap: %04X (%d)", esp_get_free_heap_size(), esp_get_free_heap_size());
        // tg_timer_init(TIMER_GROUP_0, TIMER_0);
      //  use a pollingcomponent and change the default polling interval from 16ms to 8ms to enable
      //   the system to not miss a response window on commands.

      publishSystemStatus(STATUS_ONLINE, 1);
      publishStatus(SAC, true, 1);

      vista.lrrSupervisor = _lrrSupervisor; // if we don't have a monitoring lrr supervisor we emulate one if set to true

      setDefaultKpAddr(_defaultPartition);

      for (uint8_t p = 0; p < _maxPartitions; p++)
      {
        partitionStates[p]=partitionStates_INIT;
        _partitions[p] = 0;
        publishSystemStatus(STATUS_NOT_READY, p + 1);
        publishBeeps(0, p + 1);
      }
      publishLrrMsg("ESP Restart");
      publishRfMsg(" ");
      esp_chip_info_t info;
      esp_chip_info(&info);
      ESP_LOGE(TAG, "Cores: %d", info.cores);
      ESP_LOGE(TAG,"Running on core %d",xPortGetCoreID());
      uint8_t core = info.cores > 1 ? ASYNC_CORE : 0;
      xTaskCreatePinnedToCore(
          this->cmdQueueTask, // Function to implement the task
          "cmdQueueTask",     // Name of the task
          3200,               // Stack size in words
          (void *)this,       // Task input parameter
          10,                 // Priority of the task
          &xHandle            // Task handle.
          ,
          core // Core where the task should run. 
      );

  //     // use a task on core 1 (if multicore) to setup interrupts.  We avoid running interrupt jobs on same core as network to avoid issues
  //     TaskHandle_t setupHandle;
  //           xTaskCreatePinnedToCore(
  //     this->setupTask, // setup task
  //     "setupTask",     // Name of the task
  //       1000,               // Stack size in words
  //     (void *)this,       // Task input parameter
  //     10,                 // Priority of the task
  //     &setupHandle            // Task handle.
  //     ,
  //     core // Core where the task should run. 
  // );
        ESP_LOGD(TAG, "Completed setup. Free heap=%04X (%d)",esp_get_free_heap_size(), esp_get_free_heap_size());

    }

    void vistaECPHome::alarm_disarm(std::string code, int32_t partition)
    {

      set_alarm_state("D", code, partition);
    }

    void vistaECPHome::alarm_arm_home(int32_t partition)
    {

      set_alarm_state("S", "", partition);
    }

    void vistaECPHome::alarm_arm_night(int32_t partition)
    {

      set_alarm_state("N", "", partition);
    }

    void vistaECPHome::alarm_arm_away(int32_t partition)
    {

      set_alarm_state("A", "", partition);
    }

    void vistaECPHome::alarm_trigger_fire(std::string code, int32_t partition)
    {

      set_alarm_state("F", code, partition);
    }

    void vistaECPHome::alarm_trigger_panic(std::string code, int32_t partition)
    {

      set_alarm_state("P", code, partition);
    }

    void vistaECPHome::set_zone_fault(int32_t zone, bool fault)
    {
      sensorObjType *z = getZone(zone);
       ESP_LOGD(TAG,"Setting fault %d to zone %d",fault,zone);
      if (z->zone > 0 &&  z->serial > 0  && z->emulated) {
        vista.setRFFault(fault?z->loopmask:0,z->serial);
      } else
        vista.setExpFault(zone, fault);
    }

    void vistaECPHome::alarm_keypress(std::string keystring)
    {

      alarm_keypress_partition(keystring, _defaultPartition);
    }

    void vistaECPHome::alarm_keypress_partition(std::string keystring, int32_t partition)
    {

      if (!partition)
        partition = _defaultPartition;
        

      if (partition > _maxPartitions || partition < 1)
        return;


      if (_debug > 0)
        Serial.printf("Writing keys: %s to partition %d\n", keystring.c_str(), partition);

      if (keystring == "R")
      {
        forceRefreshGlobal = true;
        _forceRefresh = true;
        return;
      }


      if (keystring == "A" || keystring == "W")
      {
        set_alarm_state("A", "", partition);
        return;
      }
      if (keystring == "S")
      {
        set_alarm_state("S", "", partition);
        return;
      }
      if (keystring == "N")
      {
        set_alarm_state("N", "", partition);
        return;
      }
      if (keystring == "I")
      {
        set_alarm_state("I", "", partition);
        return;
      }
      if (keystring == "B")
      {
        set_alarm_state("B", "", partition);
        return;
      }
      if (keystring == "Y")
      {
        set_alarm_state("Y", "", partition);
        return;
      }
      uint8_t addr = _partitionKeypads[partition];
      if (addr > 0 and addr < 24)
        vista.write(keystring.c_str(), addr);
    }

    void vistaECPHome::send_cmd_bytes(int32_t addr, std::string hexbytes)
    {
      ESP_LOGD(TAG, "Cmd bytes=%s", hexbytes.c_str());
      std::string::iterator end_pos = std::remove(hexbytes.begin(), hexbytes.end(), ' ');
      hexbytes.erase(end_pos, hexbytes.end());

      int NumberChars = hexbytes.length();
      if (NumberChars < 2 || (NumberChars % 2) != 0) {
        return;
      }
      std::vector<char> bytes((size_t)(NumberChars / 2));
      for (int i = 0; i < NumberChars; i += 2) {
        bytes[(size_t)(i / 2)] = (char)toInt(hexbytes.substr(i, 2), 16);
      }
      vista.writeDirect(bytes.data(), addr, (size_t)(NumberChars / 2));
      return;
    }

    void vistaECPHome::setDefaultKpAddr(uint8_t p)
    {
      uint8_t a;
      if (p > _maxPartitions || p < 1)
        return;
      a = _partitionKeypads[p];
      if (a > 15 && a < 24)
        vista.setKpAddr(a);
    }

    bool vistaECPHome::isInt(std::string s, int base)
    {
      if (s.empty() || std::isspace(s[0]))
        return false;
      char *p;
      strtol(s.c_str(), &p, base);
      return (*p == 0);
    }

    long int vistaECPHome::toInt(std::string s, int base)
    {
      if (s.empty() || std::isspace(s[0]))
        return 0;
      char *p;
      long int li = strtol(s.c_str(), &p, base);
      return li;
    }

    bool vistaECPHome::areEqual(char *a1, char *a2, uint8_t len)
    {
      for (int x = 0; x < len; x++)
      {
        if (a1[x] != a2[x])
          return false;
      }
      return true;
    }

    void vistaECPHome::updateDisplayLines(uint8_t partition)
    {

      uint8_t pos = vistaCmd->statusFlags.promptPos;
      std::string p1 = vistaCmd->statusFlags.prompt1;
      std::string p2 = vistaCmd->statusFlags.prompt2;
      if (pos > 0)
      {
        char buf[10];
        std::string sub1, sub2;
        if (pos > 15)
        {
          sub1 = p2.substr(0, pos - 16);
          if (pos < 31)
            sub2 = p2.substr(pos - 15);
          sprintf(buf, "[%c]", p2[pos - 16]);
          p2 = sub1 + std::string(buf) + sub2;
        }
        else
        {
          sub1 = p1.substr(0, pos);
          if (pos < 15)
            sub2 = p1.substr(pos + 1);
          sprintf(buf, "[%c]", p2[pos]);
          p1 = sub1 + std::string(buf) + sub2;
        }
      }
      publishLine1(p1.c_str(), partition);
      publishLine2(p2.c_str(), partition);
    }

  //  void vistaECPHome::getNameFromPrompt(char *p1, char *p2,std::string & out)
  //   {
  //     if (vistaCmd->cbuf[0] != 0xf7)
  //     {
  //       return;
  //     }
  //     std::string p = std::string(p1) + std::string(p2);

  //     MatchState ms;
  //     char buf[5];
  //     char buf1[20];
  //     ms.Target((char *)p.c_str());
  //     char res = ms.Match("[%a]+%s+([%d]+)%s*(.*)");
  //     if (res == REGEXP_MATCHED)
  //     {
  //       ms.GetCapture(buf, 0);
  //       ms.GetCapture(buf1, 1);
  //       ESP_LOGD(TAG, "name match=%s,zone=%s", buf1, buf);
  //       out= std::string(buf1);
  //     }
  //   }

    int vistaECPHome::getZoneFromPrompt(char *p1)
    {
      if (vistaCmd->cbuf[0] != 0xf7)
      {
        return 0;
      }
      MatchState ms;
      char buf[5];
      ms.Target(p1);
      char res = ms.Match("[%a]+%s+([%d]+)%s*(.*)");
      if (res == REGEXP_MATCHED)
      {
        ms.GetCapture(buf, 0);
        int z = toInt(buf, 10);
        vistaCmd->statusFlags.zone = z;
        ESP_LOGD(TAG, "zone match=%d", z);
        return z;
      }
      return 0;
    }

    void vistaECPHome::printPacket(const char *label, char cbuf[], int len)
    {
      char s1[4];

      std::string s = "";

      for (int c = 0; c < len; c++)
      {
        sprintf(s1, "%02X ", cbuf[c]);
        s = s.append(s1);
      }
      Serial.printf("%s: %s\n", label, s.c_str());
    }

    void vistaECPHome::set_alarm_state(std::string const &state, std::string code, int partition)
    {

      if (code.length() != 4 || !isInt(code, 10))
        code = _accessCode; // ensure we get a numeric 4 digit code

      uint8_t addr = 0;
      if (partition > _maxPartitions || partition < 1)
        return;
      addr = _partitionKeypads[partition];
      if (addr < 1 || addr > 23)
        return;

      // Arm stay
      if (state.compare("S") == 0 && !partitionStates[partition - 1].previousLightState.armed)
      {
        if (_quickArm)
          vista.write("#3", addr);
        else if (code.length() == 4)
        {
          vista.write(code.c_str(), addr);
          vista.write("3", addr);
        }
      }
      // Arm away
      else if ((state.compare("A") == 0 || state.compare("W") == 0) && !partitionStates[partition - 1].previousLightState.armed)
      {

        if (_quickArm)
          vista.write("#2", addr);
        else if (code.length() == 4)
        {
          vista.write(code.c_str(), addr);
          vista.write("2", addr);
        }
      }
      else if (state.compare("I") == 0 && !partitionStates[partition - 1].previousLightState.armed)
      {
        if (_quickArm)
          vista.write("#7", addr);
        else if (code.length() == 4)
        {
          vista.write(code.c_str(), addr);
          vista.write("7", addr);
        }
      }
      else if (state.compare("N") == 0 && !partitionStates[partition - 1].previousLightState.armed)
      {

        if (_quickArm)
          vista.write("#33", addr);
        else if (code.length() == 4)
        {
          vista.write(code.c_str(), addr);
          vista.write("33", addr);
        }
      }
      // Fire command
      else if (state.compare("F") == 0)
      {
        vista.write("F", addr);
        // todo
      }
      // Panic command
      else if (state.compare("P") == 0)
      {
        vista.write("P", addr);
      }
      else if (state.compare("B") == 0)
      {
        if (code.length() == 4)
        {
          vista.write(code.c_str(), addr);
          vista.write("6#", addr);
        }
      }
      else if (state.compare("Y") == 0)
      {
        if (code.length() == 4)
        {
          vista.write(code.c_str(), addr);
          vista.write("600", addr);
        }
      }
      else if (state.compare("D") == 0)
      {
        if (code.length() == 4)
        { // ensure we get 4 digit code
          vista.write(code.c_str(), addr);
          vista.write("1", addr);
          vista.write(code.c_str(), addr);
          vista.write("1", addr);
        }
      }
    }

    int vistaECPHome::getZoneFromChannel(uint8_t deviceAddress, uint8_t channel)
    {

      switch (deviceAddress)
      {
      case 7:
        return channel + 8;
      case 8:
        return channel + 16;
      case 9:
        return channel + 24;
      case 10:
        return channel + 32;
      case 11:
        return channel + 40;
      default:
        return 0;
      }
    }

    void vistaECPHome::assignPartitionToZone(sensorObjType *zt)
    {

      for (int p = 1; p < 4; p++)
      {
        if (_partitions[p - 1])
        {
          ESP_LOGD(TAG, "Assigning partition %d, to zone %d", p, zt->zone);
          zt->partition = p;
          break;
        }
      }
    }

    void vistaECPHome::getPartitionsFromMask()
    {
      _partitionTargets = 0;
      memset(_partitions, 0, _maxPartitions);
      for (uint8_t p = 1; p <= _maxPartitions; p++)
      {
        for (int8_t i = 3; i >= 0; i--)
        {
          int8_t shift = _partitionKeypads[p] - (8 * i);
          if (shift >= 0 && (vistaCmd->statusFlags.keypad[i] & (0x01 << shift)))
          {
          //  ESP_LOGD("test","i=%d,p=%d,shift=%02X,and=%02x,active=%d,mask=%02x",i,p,shift,(vistaCmd->statusFlags.keypad[i] & (0x01 << shift)),partitionStates[p - 1].active,vistaCmd->statusFlags.keypad[i]);
            _partitionTargets = _partitionTargets + 1;
            if (!partitionStates[p - 1].active) {
                forceRefreshGlobal = true;//new partition so we update it's sensors
                partitionStates[p - 1].active=true;
            }
            _partitions[p - 1] = 1;
            break;
          }
        }
      }
    }


    bool vistaECPHome::sendAuiTime()
    {
      time_t sec = time(nullptr);
      struct tm *tp = localtime(&sec);
      if (sec < 1000000000 || tp == nullptr || vistaCmd->statusFlags.programMode || !_auiAddr ||
          (auiCmd.state != rsidle && auiCmd.state != rsdate))
        return false;
      struct tm t = *tp;
      ESP_LOGD(TAG, "Setting AUI time...");
      char bytes[] = {00, 0x68, 0x05, 0x02, 0x45, 0x43, 0xF5, 0xEC, 0x32, 0x34, 0x31, 0x31, 0x31, 0x35, 0x31, 0x31, 0x34, 0x31, 0x30, 0x35, 0x35, 0};
      _auiSeq = _auiSeq == 0xf ? 8 : _auiSeq + 1;
      bytes[1] = 0x60 + _auiSeq;
      auiCmd.state = rsdate;
      auiCmd.time = millis();
      auiCmd.pending = true;
      int y2 = (t.tm_year + 1900) % 100;
      int w = t.tm_wday;
      snprintf(&bytes[8], 14, "%02d%02d%02d%02d%02d%02d%1d", y2, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, w);
      vista.writeDirect(bytes, _auiAddr, sizeof(bytes) - 1);
      return true;
    }

    void vistaECPHome::sendZoneRequest()
    {
      if (!_auiAddr || !(auiCmd.state == rsopenzones || auiCmd.state == rsbypasszones) || auiCmd.pending)
        return;
      _auiSeq = _auiSeq == 0xf ? 8 : _auiSeq + 1;
      char bytes[] = {00, 0x68, 0x62, 0x31, 0x45, 0x49, 0xF5, 0x31, 0xFB, 0x45, 0x4A, 0xF5, 0x32, 0xFB, 0x45, 0x43, 0xF5, 0x31, 0xFB, 0x43, 0x6C};
      bytes[1] = 0x60 + _auiSeq;
      bytes[7] = auiCmd.partition;
      bytes[12] = auiCmd.state == rsopenzones ? 0x32 : 0x35;
      auiCmd.pending = true;
      auiCmd.time = millis();
      ESP_LOGD(TAG, "Sending zone status request %d, header %02X, _auiAddr %d", auiCmd.state, bytes[1], _auiAddr);
      vista.writeDirect(bytes, _auiAddr, sizeof(bytes));
    }


    char *vistaECPHome::parseAUIMessage(char *cmd)
    {

      cmd[cmd[1] + 1] = 0; // 0 to terminate cmd to use as string
      char *c = &cmd[8];   // advance to start of fe xx byte
      char *f = NULL;
      for (uint8_t x = 0; x < cmd[1] - 7; x++)
      { // convert 0 to comma
        c[x] = !c[x] ? ',' : c[x];
      }
      if (auiCmd.state == rsopenzones || auiCmd.state == rsbypasszones)
      {
        char s[] = {0xfe, 0xfe, 0xfe, 0xfe, 0};
        f = strstr(c, s);
        if (f)
        {
          f = f + strlen(s);
          if (*f == 0xec)
            f++;
          return f;
        }
      }
      else if (auiCmd.state == rszoneinfo)
      {
        char s[] = {0xfe, 0xfe, 0xfe, 0};
        f = strstr(c, s);
        if (f)
        {
          f = f + strlen(s);
          if (*f == 0xec)
            f++;
          return f;
        }
      }
      else if (auiCmd.state == rszonecount)
      {
        char s[] = {0xfe, 0xfe, 0};
        f = strstr(c, s);
        if (f)
          return f + strlen(s);
      }
      else if (auiCmd.state == rsidle)
      {
        char s[] = {0xf5, 0xec, 0};
        f = strstr(c, s);
        if (f)
          return f + strlen(s);
      }
      else if (auiCmd.state == rsdate)
      {
        char s[] = {0xfe, 0};
        f = strstr(c, s);
        if (f)
          return f + strlen(s);
      }
      else
      {
        char s[] = {0xfd, 0}; // error
        f = strstr(c, s);
        if (f)
          return f + strlen(s);
      }
      return NULL;
    }

    void vistaECPHome::updateZoneState(sensorObjType *zt, int p, bool state, unsigned long t)
    {
      if (!zt->zone) return;
      zt->partition = p;
      zt->time = t;
      if (auiCmd.state == rsopenzones)
      {
        zt->open = state;
        zoneStatusUpdate(zt);
        ESP_LOGD(TAG, "Setting open zone %d to %d,  partition %d", zt->zone, state, p);
      }
      else if (auiCmd.state == rsbypasszones)
      {
        zt->bypass = state;
        ESP_LOGD(TAG, "Setting bypass zone %d to %d, partition %d", zt->zone, state, p);
      }
    }

    void vistaECPHome::processZoneList(char *list)
    {
      std::string s = "";
      if (list)
      {
        s = list;
      }
      s.append(",");

      ESP_LOGD(TAG, "Zones: %s", s.c_str());
      uint8_t p = auiCmd.partition - 0x30; // set 0x31 - 0x34 to 1 - 4 range

      // Search all occurences of integers or ranges
      unsigned long t = millis();
      size_t pos;
      char buf[5], buf1[5];
      MatchState ms;
      while ((pos = s.find(',')) != std::string::npos)
      {
        std::string s1 = s.substr(0, pos);
        ms.Target((char *)s1.c_str());
        char res = ms.Match("(%d+)-(%d+)");
        if (res == REGEXP_MATCHED)
        {
          ms.GetCapture(buf, 0);
          ms.GetCapture(buf1, 1);
          // Yes, range, add all values within to the vector
          for (int z{std::stoi(buf)}; z <= std::stoi(buf1); ++z)
          {
            updateZoneState(getZone(z), p, true, t);
          }
        }
        else
        {
          res = ms.Match("(%d+)");
          if (res == REGEXP_MATCHED)
          {
            ms.GetCapture(buf, 0);
            // No, no range, just a plain integer value. Add it to the vector
            int z = std::stoi(buf);
            updateZoneState(getZone(z), p, true, t);
          }
        }
        s.erase(0, pos + 1); /* erase() function store the current positon and move to next token. */
      }

      // clear  bypass/open zones for partition p that were not set above
      auto it = std::find_if(extZones.begin(), extZones.end(), [&p, &t](sensorObjType &f)
                             { return (f.partition == p && f.active && f.time != t && (f.open || f.bypass)); });

      while (it != extZones.end())
      {

        updateZoneState(&(*it), p, false, millis());

        it = std::find_if(++it, extZones.end(), [&p, &t](sensorObjType &f)
                          { return (f.partition == p && f.active && f.time != t && (f.open || f.bypass)); });
      }

      forceRefreshZones = true;
    }


    // void vistaECPHome::setupTask(void *args)
    // {
    //   //ensure we run vista setup on correct core
    //   vistaECPHome *_this =  reinterpret_cast<vistaECPHome *> (args);
    //   vista.begin(_this->_rxPin, _this->_txPin, _this->_keypadAddr1, _this->_monitorPin, _this->_invertRx, _this->_invertTx, _this->_invertMon, _this->_inputRx, _this->_inputMon);
    //   vTaskDelete(NULL); //exit task as we are done
    // }

    void vistaECPHome::cmdQueueTask(void *args)
    {
      //ensure we run the vista interrupts on core 1 for multicore esp32 devices
      
     vistaECPHome *_this = reinterpret_cast<vistaECPHome *> (args);
     vista.begin(_this->_rxPin, _this->_txPin, _this->_keypadAddr1, _this->_monitorPin, _this->_invertRx, _this->_invertTx, _this->_invertMon, _this->_inputRx, _this->_inputMon);
     
      unsigned long checkTime = millis();
      unsigned long dataTime = millis();
      bool dataTimeout = false;

      for (;;)
      {

        if (!vista.handle())
        {
          vTaskDelay(4 / portTICK_PERIOD_MS);
          if (millis() - checkTime > 60000)
          {
            checkTime = millis();
          }
          if (millis() - dataTime > 60000)
          {
            dataTime = millis();
            ESP_LOGE(TAG, "Data timeout. Is the panel connected?");
            vista.connected = false;
          }
        }
        else
        {
          dataTime = millis();
          vista.connected = true;
        }

      }
      vTaskDelete(NULL);
    }


      void vistaECPHome::loop()
      {

        static bool firstRun=false;
        static bool lastConnectState=false;
        bool is_connected = true;
        if (is_connected && is_connected != lastConnectState) firstRun=true;
        lastConnectState=is_connected;
        
        processAuiQueue();
      static unsigned long checkTime = millis();
      if (millis() - checkTime > 10000)
      {
        checkTime = millis();
        UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGD(TAG, "Stack high water mark: %5d", (uint16_t)uxHighWaterMark);
      }


        // if data to be sent, we ensure we process it quickly to avoid delays with the F6 cmd


        if (vista.cmdAvail())
        {

          vistaCmd = vista.getNextCmd();
          if (vistaCmd==NULL) return;  //should not happen but exit if it does
          static unsigned long refreshTime = millis();

          if (firstRun  || millis() - refreshTime > 60000)
          {
            forceRefreshZones = true;
            forceRefreshGlobal = true;
            refreshTime = millis();
          }

          // rf testing code

          // static unsigned long testtime = millis();
          // static char t1 = 0;
          // if (!vistaCmd->newExtCmd && millis() - testtime > 15000)
          // {
          //   // FB 04 09 9A C4 80 00 00 00 00 00 00 00
          //   // 0629444,80 (loop 1)
          //   vistaCmd->newExtCmd = true;
          //   vistaCmd->cbuf[0] = 0xfb;
          //   vistaCmd->cbuf[1] = 4;
          //   vistaCmd->cbuf[2] = 9;
          //   vistaCmd->cbuf[3] = 0x9a;
          //   vistaCmd->cbuf[4] = 0xc4;
          //   vistaCmd->cbuf[5] = 0x02;

          //   if (t1 == 1)
          //   {
          //     t1 = 2;
          //     vistaCmd->cbuf[5] = 0x80;
          //   }
          //   else if (t1 == 2)
          //   {
          //     t1 = 1;
          //     vistaCmd->cbuf[5] = 0x2;
          //   }
          //   else if (t1 == 0)
          //     t1 = 1;

          //   testtime = millis();
          // }

          if (!vistaCmd->newExtCmd && !vistaCmd->newCmd && _debug > 0)
          {
            // if (vistaCmd->cbuf[0] == 0xF7)
            //   printPacket("CHK", vistaCmd->cbuf, vistaCmd->size);
            // else
            //   printPacket("CHK", vistaCmd->cbuf, 13);
            // return;
            printPacket("CHK", vistaCmd->cbuf, vistaCmd->size);
          }

          static unsigned long refreshLrrTime, refreshRfTime;
          // process ext messages for zones
          if (vistaCmd->newExtCmd)
          {
            if (_debug > 0)
            {
              // if (vistaCmd->cbuf[0] == 0xF6) {
              //   printPacket("EXT", vistaCmd->cbuf, vistaCmd->cbuf[3] + 4);
              //   if (_debug > 2)
              //      printPacket("RAW",vistaCmd->extbuf,vistaCmd->cbuf[3] + 2);
              // } else {
              //   printPacket("EXT", vistaCmd->cbuf, 13);
              //   if (_debug > 2)
              //     printPacket("RAW",vistaCmd->extbuf,13);
              // }
              printPacket("EXT", vistaCmd->cbuf, vistaCmd->size);
              if (_debug > 2)
                printPacket("RAW", vistaCmd->extbuf, vistaCmd->rawsize);

              // format: [0xFA] [deviceid] [subcommand] [channel/zone] [on/off] [relaydata]
            }
            if (vistaCmd->cbuf[0] == 0xFA)
            {
              int z = vistaCmd->cbuf[3];
              if (vistaCmd->cbuf[2] == 0xf1 && z > 0 && z <= _maxZones)
              { // we have a zone status (zone expander address range)
                if (_debug > 2)
                  ESP_LOGD(TAG, "FA status update to zone %d",z);
                sensorObjType *zt = getZone(z);

                if (zt->active)
                {
                  zt->time = millis();
                  zt->open = vistaCmd->cbuf[4];
                  zt->external = zt->open;
                  zoneStatusUpdate(zt);
                }
              }
              else if (vistaCmd->cbuf[2] == 0x00)
              { // relay update z = 1 to 4
                if (z > 0)
                {
                  publishRelayStatus(vistaCmd->cbuf[1], z, vistaCmd->cbuf[4] ? true : false);
                  if (_debug > 0)
                    Serial.printf("Got relay address %d channel %d = %d\n", vistaCmd->cbuf[1], z, vistaCmd->cbuf[4]);
                }
              }
              else if (vistaCmd->cbuf[2] == 0x0d)
              { // relay update z = 1 to 4 - 1sec on / 1 sec off
                if (z > 0)
                {
                  // relaypublishStatus(vistaCmd->cbuf[1],z,vistaCmd->cbuf[4]?true:false);
                  if (_debug > 0)
                    Serial.printf("Got relay address %d channel %d = %d. Cmd 0D. Pulsing 1sec on/ 1sec off\n", vistaCmd->cbuf[1], z, vistaCmd->cbuf[4]);
                }
              }
              else if (vistaCmd->cbuf[2] == 0xf7)
              { // 30 second zone expander module status update
                uint8_t faults = vistaCmd->cbuf[4];
                for (int x = 8; x > 0; x--)
                {
                  z = getZoneFromChannel(vistaCmd->cbuf[1], x); // device id=extcmd[1]
                  if (!z)
                    continue;
                  bool zs = faults & 1 ? true : false; // check first bit . lower bit = channel 8. High bit= channel 1
                  faults = faults >> 1;                // get next zone status bit from field
                  sensorObjType *zt = getZone(z);
                  if (zt->open != zs && zt->active)
                  {
                    zt->open = zs;
                    zoneStatusUpdate(zt);
                  }
                  zt->time = millis();
                }
              }
            }
            else if (vistaCmd->cbuf[0] == 0xFB && vistaCmd->cbuf[1] == 4)
            {

              // char rf_serial_char[14];
              char rf_serial_char_out[20];
              // FB 04 06 18 98 B0 00 00 00 00 00 00
              // FB 04 09 9A C4 80 00 00 00 00 00 00 00
              uint32_t device_serial = (vistaCmd->cbuf[2] << 16) + (vistaCmd->cbuf[3] << 8) + vistaCmd->cbuf[4];
              // vistaCmd->cbuf[5] is loop and battery is bit 1

              // snprintf(rf_serial_char, 14, "%03d%04d", device_serial / 10000, device_serial % 10000);
              sensorObjType *zt = getZoneFromSerial(device_serial);
              if (_debug)
              {
                Serial.printf("RFX: %d,%02x\n", device_serial, vistaCmd->cbuf[5]);
              }
              if (zt->active && !(vistaCmd->cbuf[5] & 4) && !(vistaCmd->cbuf[5] & 1))
              {
                zt->time = millis();
                zt->open = vistaCmd->cbuf[5] & zt->loopmask ? true : false;
                zt->external = zt->open; //if zone is open we flag it as external so we dont reset it later
                zt->rflowbat = vistaCmd->cbuf[5] & 2 ? true : false; // low bat
                // ESP_LOGD(TAG, "set rf low bat to %d", zt->rflowbat);
                zoneStatusUpdate(zt);
              }

              sprintf(rf_serial_char_out, "%d,%02x", device_serial, vistaCmd->cbuf[5]);
              publishRfMsg(rf_serial_char_out);
              refreshRfTime = millis();
            }
            /* rf_serial_char

                1 - ? (loop flag?)
                2 - Low battery
                3 -	Supervision required /heartbeat
                4 - ?
                5 -	Loop 3
                6 -	Loop 2
                7 -	Loop 4
                8 -	Loop 1

            */
           else if (vistaCmd->cbuf[0] == 0xF0 && vistaCmd->cbuf[1] > 0)
            {
               //f0 xx C8 87 00 xx xx xx 00 00 A9
              uint32_t device_serial = (vistaCmd->cbuf[5] << 16) + (vistaCmd->cbuf[6] << 8) + vistaCmd->cbuf[7];
              sensorObjType *zt = getZoneFromSerial(device_serial);
              {
                Serial.printf("LOOP: %d,%02x\n", device_serial, vistaCmd->cbuf[5]);
              }
              if (zt->active && !(vistaCmd->cbuf[5] & 4) && !(vistaCmd->cbuf[5] & 1))
              {

               // zt->time = millis();
                //zt->open = vistaCmd->cbuf[5] & zt->loopmask ? true : false;
               // zt->external = zt->open; //if zone is open we flag it as external so we dont reset it later
                //zoneStatusUpdate(zt);
              }

             // sprintf(rf_serial_char_out, "%d,%02x", device_serial, vistaCmd->cbuf[5]);
             // publishRfMsg(rf_serial_char_out);
             // refreshRfTime = millis();
            }
          }
          else

              if (_debug > 0 && vistaCmd->newCmd)
          {
            // if (vistaCmd->cbuf[0] == 0xF2)
            //   printPacket("CMD", vistaCmd->cbuf, vistaCmd->cbuf[1] + 2);
            // else
            //   printPacket("CMD", vistaCmd->cbuf, 13);

            printPacket("CMD", vistaCmd->cbuf, vistaCmd->size);
          }

          if (vistaCmd->newCmd && _auiAddr && vistaCmd->cbuf[0] == 0xF2)
          {
            if (auiCmd.state != rsidle)
              ESP_LOGD(TAG, "AUI cmd state: %d, pending: %d", auiCmd.state, auiCmd.pending);
            // if ((vistaCmd->cbuf[2] >> 1) & _auiAddr)
            //  activeAuiAddr=true;
            if (((vistaCmd->cbuf[2]) & _auiAddrMask) && (vistaCmd->cbuf[7] & 0xf0) == 0x60 && vistaCmd->cbuf[8] == 0x63 && vistaCmd->cbuf[9] == 0x02)
            { // partition update broadcast
              char *m = parseAUIMessage(vistaCmd->cbuf);
              if (m == NULL)
                return;

              size_t l = &vistaCmd->cbuf[1] + vistaCmd->cbuf[1] - m;
              // ESP_LOGD(TAG, "m length = %d,byte=%02X", l, m[0]);
              // if (m[0] & 1)
              // {
              if (auiCmd.state == rsidle)
              {
                auiCmd.state = rsopenzones;
                auiCmd.partition = vistaCmd->cbuf[13];
                auiCmd.pending = false;
                sendZoneRequest();
              }
              else if (auiCmd.state != rsopenzones && auiCmd.state != rsbypasszones)
              {
                auiCmdType c;
                c.state = rsopenzones;
                c.partition = vistaCmd->cbuf[13];
                if (auiQueue.size() < 5)
                  auiQueue.push(c);
              }
              //   }
              // else
              if (l > 4 && m[0] == 2)
              {
                // we have an exit delay
                // exitDelay=m[5] for partition partitionRequest
              }
            }
            else if (((vistaCmd->cbuf[2] ) & _auiAddrMask) && (vistaCmd->cbuf[7] & 0xf0) == 0x50 && vistaCmd->cbuf[8] == 0xfe && vistaCmd->cbuf[10] != 0xfd)
            { // response data from request
              char *m = parseAUIMessage(vistaCmd->cbuf);
              if (m == NULL)
                return;
              auiCmd.time = millis();
              ESP_LOGD(TAG, "success message from %d", auiCmd.state);
              auiCmd.pending = false;
              if (auiCmd.state == rsopenzones || auiCmd.state == rsbypasszones)
              {
                processZoneList(m);

                if (auiCmd.state == rsopenzones)
                {
                  auiCmd.state = rsbypasszones;
                  sendZoneRequest();
                }
                else
                  auiCmd.state = rsidle;
              }
              else if (auiCmd.state == rsdate)
              {
                auiCmd.state = rsidle;
              }
            }
            else if (((vistaCmd->cbuf[2] ) & _auiAddrMask) && (vistaCmd->cbuf[7] & 0xf0) == 0x50 && (vistaCmd->cbuf[8] == 0xfd || vistaCmd->cbuf[10] == 0xfd))
            {
              char *m = parseAUIMessage(vistaCmd->cbuf);
              if (m == NULL)
                return;
              auiCmd.time = millis();
              auiCmd.pending = false;
              ESP_LOGD(TAG, "failure message from %d", auiCmd.state);
              if (auiCmd.state == rszoneinfo)
              {
                auiCmd.record++;
                if (auiCmd.record > auiCmd.records)
                  auiCmd.state = rsidle;
              }
              else if (auiCmd.state == rsdate)
              {
                // dateReqStatus=-1;
                auiCmd.state = rsidle;
              }
              else
                auiCmd.state = rsidle;
            }
            return;
          }
          else if (vistaCmd->newCmd && vistaCmd->cbuf[0] == 0xf7)
          {

            getPartitionsFromMask();

            for (uint8_t partition = 1; partition <= _maxPartitions; partition++)
            {
              if (_partitions[partition - 1])
              {
                Serial.printf("Partition: %02X\n", partition);
                updateDisplayLines(partition);
                publishBeeps(vistaCmd->statusFlags.beeps, partition);
                if (vistaCmd->statusFlags.systemFlag && strstr(vistaCmd->statusFlags.prompt2, HITSTAR))
                  alarm_keypress_partition("*", partition);
              }
            }
            Serial.printf("Prompt: %s\n", vistaCmd->statusFlags.prompt1);
            Serial.printf("Prompt: %s\n", vistaCmd->statusFlags.prompt2);
            Serial.printf("Beeps: %d\n", vistaCmd->statusFlags.beeps);
          
          }

          // publishes lrr status messages
          if ((vistaCmd->newCmd && vistaCmd->cbuf[0] == 0xf9 && vistaCmd->cbuf[3] == 0x58) || firstRun)
          { // we show all lrr messages with type 58

            int c = vistaCmd->statusFlags.lrr.code;
            int q = vistaCmd->statusFlags.lrr.qual;
            int z = vistaCmd->statusFlags.lrr.data; // can be zone or user
            int p = vistaCmd->statusFlags.lrr.partition;

            std::string qual;
            char msg[100];
            if (c < 400)
              qual = (q == 3) ? " is Cleared" : " ";
            else if (c == 570)
              qual = (q == 1) ? " is Active" : " is Cleared";
            else
              qual = (q == 1) ? " is Restored" : " ";
            if (c)
            {
              std::string lrrString = FC(statusText(c));
              std::string zn = std::to_string(z);
              std::string uf = "by user";
              if (lrrString[0] == 'Z')
              {
                uf = "on zone";
                getZoneName(z,zn);
              }

              snprintf(msg, 100, "CID_%d%03d: %s %s %s%s, Partition %d", q, c, &lrrString[1], uf.c_str(), zn.c_str(), qual.c_str(), p);

              publishLrrMsg(msg);
              refreshLrrTime = millis();
            }
          }

          // done other cmd processing.  Process f7 now
          if (!vistaCmd->newCmd || vistaCmd->cbuf[0] != 0xf7 || vistaCmd->cbuf[12] == 0x77)
            return;


          currentLightState.stay = false;
          currentLightState.away = false;
          currentLightState.night = false;
          currentLightState.instant = false;
          currentLightState.ready = false;
          currentLightState.alarm = false;
          currentLightState.armed = false;
          currentLightState.ac = true;
          currentLightState.fire = false;
          currentLightState.check = false;
          currentLightState.trouble = false;
          currentLightState.bypass = false;
          currentLightState.chime = false;

          bool updateSystemState = false;
          _currentSystemState = sunavailable;


          // Publishes ready status

          if (vistaCmd->statusFlags.ready)
          {
            _currentSystemState = sdisarmed;
            currentLightState.ready = true;
            updateSystemState = true;
          }
          // armed status lights
          if (vistaCmd->statusFlags.armedAway || vistaCmd->statusFlags.armedStay)
          {
            updateSystemState = true;
            if (vistaCmd->statusFlags.night)
            {
              _currentSystemState = sarmednight;
              currentLightState.night = true;
              currentLightState.stay = true;
            }
            else if (vistaCmd->statusFlags.armedAway)
            {
              _currentSystemState = sarmedaway;
              currentLightState.away = true;
            }
            else
            {
              _currentSystemState = sarmedstay;
              currentLightState.stay = true;
            }
            currentLightState.armed = true;
          }
          // zone fire status
          // int tz;
          if (!vistaCmd->statusFlags.systemFlag && !vistaCmd->statusFlags.check && vistaCmd->statusFlags.fireZone)
          {
            if (vistaCmd->cbuf[5] > 0x90)
              getZoneFromPrompt(vistaCmd->statusFlags.prompt1);
            // if (promptContains(p1,FIRE,tz) && !vistaCmd->statusFlags.systemFlag) {
            fireStatus->zone = vistaCmd->statusFlags.zone;
            fireStatus->time = millis();
            fireStatus->state = true;
            getZone(vistaCmd->statusFlags.zone)->fire = true;
            // ESP_LOGD("test","fire found for zone %d,status=%d",vistaCmd->statusFlags.zone,fireStatus->state);
          }
          // zone alarm status
          if (!vistaCmd->statusFlags.systemFlag && !vistaCmd->statusFlags.check && vistaCmd->statusFlags.alarm)
          {
            if (vistaCmd->cbuf[5] > 0x90)
              getZoneFromPrompt(vistaCmd->statusFlags.prompt1);
            // if (promptContains(p1,ALARM,tz) && !vistaCmd->statusFlags.systemFlag) {
            sensorObjType *zt = getZone(vistaCmd->statusFlags.zone);
            if (!zt->alarm && zt->active && zt->zone)
            {
              zt->alarm = true;
              zoneStatusUpdate(zt);
            }
            if (!zt->partition && zt->active)
              assignPartitionToZone(zt);
            zt->time = millis();
            alarmStatus->zone = vistaCmd->statusFlags.zone;
            alarmStatus->time = zt->time;
            alarmStatus->state = true;
            // ESP_LOGD("test","alarm found for zone %d,status=%d",vistaCmd->statusFlags.zone,zt->alarm );
          }
          // device check status
          if (vistaCmd->statusFlags.check)
          {
            updateSystemState = true; // we also get system flags when a device has a check flag
            if (vistaCmd->cbuf[5] > 0x90)
              getZoneFromPrompt(vistaCmd->statusFlags.prompt1);
            sensorObjType *zt = getZone(vistaCmd->statusFlags.zone);
            // ESP_LOGD("test", "check found for zone %d,status=%d", vistaCmd->statusFlags.zone, zt->check);
            if (!zt->check && zt->active && zt->zone)
            {
              zt->check = true;
              zt->open = false;
              zt->alarm = false;
              currentLightState.trouble = true;
              zoneStatusUpdate(zt);
              // ESP_LOGD("test", "updating check zone %d,status=%d", vistaCmd->statusFlags.zone, zt->check);
            }
            if (!zt->partition && zt->active)
              assignPartitionToZone(zt);
            zt->time = millis();
          }
          // zone fault status
          // ESP_LOGD("test","armed status/system,stay,away flag is: %d , %d, %d , %d",vistaCmd->statusFlags.armed,vistaCmd->statusFlags.systemFlag,vistaCmd->statusFlags.armedStay,vistaCmd->statusFlags.armedAway);
          // if (!(vistaCmd->cbuf[7] > 0 || vistaCmd->statusFlags.beeps == 1 || vistaCmd->statusFlags.beeps == 4) && !(vistaCmd->statusFlags.instant || vistaCmd->statusFlags.armedAway || vistaCmd->statusFlags.armedStay || vistaCmd->statusFlags.night))
          // En modo programacion el texto del teclado no es una zona en falla.
          if (!vistaCmd->statusFlags.systemFlag && !vistaCmd->statusFlags.check && !vistaCmd->statusFlags.bypass && !vistaCmd->statusFlags.alarm && !vistaCmd->statusFlags.programMode && !(vistaCmd->statusFlags.instant || vistaCmd->statusFlags.armedAway || vistaCmd->statusFlags.armedStay || vistaCmd->statusFlags.night))
          {
            if (vistaCmd->cbuf[5] > 0x90)
              getZoneFromPrompt(vistaCmd->statusFlags.prompt1);

            sensorObjType *zt = getZone(vistaCmd->statusFlags.zone);
            if (vistaCmd->statusFlags.lowBattery)
            {
              zt->lowbat = vistaCmd->statusFlags.lowBattery;
            }
            else if (!zt->open && zt->active && zt->zone)
            {
              zt->open = true;
              zt->check = false;
              zt->bypass = false;
              zoneStatusUpdate(zt);
            }
            zt->external=false;
            if (!zt->partition && zt->active)
              assignPartitionToZone(zt);

            // ESP_LOGD("test","fault found for zone %d,status=%d",vistaCmd->statusFlags.zone,zt->open);
            zt->time = millis();
          }
          // zone bypass status
          // if (vistaCmd->cbuf[0] == 0xf7 && !(vistaCmd->statusFlags.systemFlag || vistaCmd->statusFlags.armedAway || vistaCmd->statusFlags.armedStay || vistaCmd->statusFlags.fire || vistaCmd->statusFlags.check || vistaCmd->statusFlags.alarm || vistaCmd->statusFlags.night || vistaCmd->statusFlags.instant) && vistaCmd->statusFlags.bypass && vistaCmd->statusFlags.beeps == 1)
          if (!vistaCmd->statusFlags.systemFlag && !vistaCmd->statusFlags.check && vistaCmd->statusFlags.bypass && !vistaCmd->statusFlags.alarm && !(vistaCmd->statusFlags.instant || vistaCmd->statusFlags.armedAway || vistaCmd->statusFlags.armedStay || vistaCmd->statusFlags.night))
          {
            if (vistaCmd->cbuf[5] > 0x90)
              getZoneFromPrompt(vistaCmd->statusFlags.prompt1);
            // if (promptContains(p1,BYPAS,tz) && !vistaCmd->statusFlags.systemFlag) {

            sensorObjType *zt = getZone(vistaCmd->statusFlags.zone);

            if (!zt->bypass && zt->active && zt->zone)
            {
              zt->bypass = true;
              zoneStatusUpdate(zt);
            }
            if (!zt->partition && zt->active)
              assignPartitionToZone(zt);
            zt->time = millis();

            // ESP_LOGD("test","bypass found for zone %d,status=%d",vistaCmd->statusFlags.zone,zt->bypass);
          }

          // trouble lights
          if (!vistaCmd->statusFlags.acPower)
          {
            currentLightState.ac = false;
          }

          if (vistaCmd->statusFlags.lowBattery && (vistaCmd->statusFlags.systemFlag || vistaCmd->statusFlags.check))
          {
            currentLightState.bat = true;
            _lowBatteryTime = millis();
          }
          // ESP_LOGE(TAG,"ac=%d,batt status = %d,systemflag=%d,lightbat status=%d,trouble=%d", currentLightState.ac,vistaCmd->statusFlags.lowBattery,vistaCmd->statusFlags.systemFlag,currentLightState.bat,currentLightState.trouble);

          if (vistaCmd->statusFlags.fire)
          {
            currentLightState.fire = true;
            _currentSystemState = striggered;
          }

          if (vistaCmd->statusFlags.inAlarm)
          {
            _currentSystemState = striggered;
            alarmStatus->zone = 99;
            alarmStatus->time = millis();
            alarmStatus->state = true;
          }

          if (vistaCmd->statusFlags.chime)
          {
            currentLightState.chime = true;
          }

          if (vistaCmd->statusFlags.bypass)
          {
            currentLightState.bypass = true;
          }

          if (vistaCmd->statusFlags.check)
          {
            currentLightState.check = true;
          }
          if (vistaCmd->statusFlags.instant)
          {
            currentLightState.instant = true;
          }

          // if ( vistaCmd->statusFlags.cancel ) {
          //    currentLightState.canceled=true;
          //	}    else  currentLightState.canceled=false;
          unsigned long chkTime = millis();
          // clear alarm statuses  when timer expires
          if ((chkTime - fireStatus->time) > TTL)
          {
            fireStatus->state = false;
            if (fireStatus->zone > 0 && fireStatus->zone <= _maxZones)
              getZone(fireStatus->zone)->fire = false;
          }
          if ((chkTime - alarmStatus->time) > TTL)
          {
            alarmStatus->state = false;
            if (alarmStatus->zone > 0 && alarmStatus->zone <= _maxZones)
              getZone(alarmStatus->zone)->alarm = false;
          }
          if ((chkTime - panicStatus->time) > TTL)
          {
            panicStatus->state = false;
            if (panicStatus->zone > 0 && panicStatus->zone <= _maxZones)
              getZone(panicStatus->zone)->panic = false;
          }
          //  if ((millis() - systemPrompt.time) > TTL) systemPrompt.state = false;
          if ((chkTime - _lowBatteryTime) > TTL)
            currentLightState.bat = false;

          if (!currentLightState.ac || currentLightState.bat || vistaCmd->statusFlags.check)
            currentLightState.trouble = true;

          currentLightState.alarm = alarmStatus->state;

          if (currentLightState.bat != previousLightState.bat || forceRefreshGlobal)
            publishStatus(SBAT, currentLightState.bat);
          if (currentLightState.ac != previousLightState.ac || forceRefreshGlobal)
            publishStatus(SAC, currentLightState.ac);

          for (uint8_t partition = 1; partition <= _maxPartitions; partition++)
          {
            if ((_partitions[partition - 1] && _partitionTargets == 1) && (vistaCmd->statusFlags.systemFlag || updateSystemState))
            {
              // system status message
              _forceRefresh = partitionStates[partition - 1].refreshStatus || forceRefreshGlobal;

              if (_currentSystemState != partitionStates[partition - 1].previousSystemState || _forceRefresh || updateSystemState)
                switch (_currentSystemState)
                {
                case striggered:
                  publishSystemStatus(STATUS_TRIGGERED, partition);
                  break;
                case sarmedaway:
                  publishSystemStatus(STATUS_ARMED, partition);
                  break;
                case sarmednight:
                  publishSystemStatus(STATUS_NIGHT, partition);
                  break;
                case sarmedstay:
                  publishSystemStatus(STATUS_STAY, partition);
                  break;
                case sunavailable:
                  publishSystemStatus(STATUS_NOT_READY, partition);
                  break;
                case sdisarmed:
                  publishSystemStatus(STATUS_OFF, partition);
                  break;
                default:
                  publishSystemStatus(STATUS_NOT_READY, partition);
                }
              partitionStates[partition - 1].previousSystemState = _currentSystemState;
              partitionStates[partition - 1].refreshStatus = false;
            }
          }

          for (uint8_t partition = 1; partition <= _maxPartitions; partition++)
          {
            if ((_partitions[partition - 1] && _partitionTargets == 1))
            {
              // publish status on change only - keeps api traffic down
              previousLightState = partitionStates[partition - 1].previousLightState;

              _forceRefresh = partitionStates[partition - 1].refreshLights || forceRefreshGlobal;

              // ESP_LOGD("test","refreshing partition statuse _partitions: %d,force refresh=%d",partition,_forceRefresh);

              if (currentLightState.fire != previousLightState.fire || _forceRefresh || updateSystemState)
                publishStatus(SFIRE, currentLightState.fire, partition);
              if (currentLightState.alarm != previousLightState.alarm || _forceRefresh || updateSystemState)
                publishStatus(SALARM, currentLightState.alarm, partition);
              if ((currentLightState.trouble != previousLightState.trouble || _forceRefresh || updateSystemState))
                publishStatus(STROUBLE, currentLightState.trouble, partition);
              if (currentLightState.chime != previousLightState.chime || _forceRefresh || updateSystemState)
                publishStatus(SCHIME, currentLightState.chime, partition);
              // if (currentLightState.check != previousLightState.check || _forceRefresh)
              //   publishStatus(scheck, currentLightState.check, partition);

              if (vistaCmd->statusFlags.systemFlag || updateSystemState)
              {
                lightStates &pub = partitionStates[partition - 1].publishedArmed;
                if (currentLightState.away != pub.away || _forceRefresh)
                  publishStatus(SARMEDAWAY, currentLightState.away, partition);
                if (currentLightState.stay != pub.stay || _forceRefresh)
                  publishStatus(SARMEDSTAY, currentLightState.stay, partition);
                if (currentLightState.night != pub.night || _forceRefresh)
                  publishStatus(SARMEDNIGHT, currentLightState.night, partition);
                if (currentLightState.instant != pub.instant || _forceRefresh)
                  publishStatus(SINSTANT, currentLightState.instant, partition);
                if (currentLightState.armed != pub.armed || _forceRefresh)
                  publishStatus(SARMED, currentLightState.armed, partition);
                pub = currentLightState;
              }

              if (currentLightState.bypass != previousLightState.bypass || _forceRefresh || updateSystemState)
                publishStatus(SBYPASS, currentLightState.bypass, partition);
 
              if (currentLightState.ready != previousLightState.ready || _forceRefresh || updateSystemState) {
               // ESP_LOGD("debug","partition=%d,refresh=%d,update=%d,ready=%d,systemflag=%d",partition,_forceRefresh,updateSystemState,currentLightState.ready,vistaCmd->statusFlags.systemFlag);
               publishStatus(SREADY, currentLightState.ready, partition);
              }

              //  if (currentLightState.canceled != previousLightState.canceled)
              //   publishStatus(scanceled,currentLightState.canceled,partition);

              partitionStates[partition - 1].previousLightState = currentLightState;
              partitionStates[partition - 1].refreshLights = false;
            }
          }

          std::string zoneStatusMsg = "";
          char s1[16];
          // clears restored zones after timeout
          for (auto &x : extZones)
          {

            if (!x.active || !x.partition || !x.zone)
              continue;

            if (!x.bypass && x.open && partitionStates[x.partition - 1].previousLightState.ready && !x.external)
            {

              x.open = false;
              x.check = false;
              x.alarm = false;
              zoneStatusUpdate(&x);
            }

            if (x.bypass && !partitionStates[x.partition - 1].previousLightState.bypass)
            {
              x.bypass = false;
            }

            if (x.alarm && !partitionStates[x.partition - 1].previousLightState.alarm)
            {
              x.alarm = false;
            }

            if (!x.bypass && x.lowbat && (millis() - x.time) > TTL)
            {
              x.lowbat = false;
            }

            if (!x.bypass && x.open && (millis() - x.time) > TTL && !x.external)
            {
              x.open = false;
              zoneStatusUpdate(&x);
            }
            if (!x.bypass && x.check && (millis() - x.time) > TTL)
            {
              x.check = false;
              zoneStatusUpdate(&x);
            }

            if (forceRefreshZones || forceRefreshGlobal)
            {
              zoneStatusUpdate(&x);
            }

            if (x.open)
            {
              if (zoneStatusMsg != "")
                sprintf(s1, ",OP:%d", x.zone);
              else
                sprintf(s1, "OP:%d", x.zone);
              zoneStatusMsg.append(s1);
            }
            if (x.alarm)
            {
              if (zoneStatusMsg != "")
                sprintf(s1, ",AL:%d", x.zone);
              else
                sprintf(s1, "AL:%d", x.zone);
              zoneStatusMsg.append(s1);
            }
            if (x.bypass)
            {
              if (zoneStatusMsg != "")
                sprintf(s1, ",BY:%d", x.zone);
              else
                sprintf(s1, "BY:%d", x.zone);
              zoneStatusMsg.append(s1);
            }
            if (x.check)
            {
              if (zoneStatusMsg != "")
                sprintf(s1, ",CK:%d", x.zone);
              else
                sprintf(s1, "CK:%d", x.zone);
              zoneStatusMsg.append(s1);
            }
            if (x.lowbat || x.rflowbat)
            { // low rf battery
              if (zoneStatusMsg != "")
                sprintf(s1, ",LB:%d", x.zone);
              else
                sprintf(s1, "LB:%d", x.zone);
              zoneStatusMsg.append(s1);
            }
          }

          if ((zoneStatusMsg != _previousZoneStatusMsg || forceRefreshZones || forceRefreshGlobal))
            publishZoneExtendedStatus(zoneStatusMsg);

          _previousZoneStatusMsg = zoneStatusMsg;

          //    chkTime = millis();
          /*
                  if (chkTime - refreshLrrTime > 30000)
                  {
                    if (firstRun)
                      lrrMsgChangeCallback("ESP Restart");
                  else
                    lrrMsgChangeCallback("");
                  refreshLrrTime = chkTime;
                  }
          */
          /*
                  if (chkTime - refreshRfTime > 30000)
                  {
                    rfMsgChangeCallback("");
                    refreshRfTime = chkTime;
                  }
          */
          firstRun = false;
          forceRefreshZones = false;
          forceRefreshGlobal = false;
        }

      }

      const __FlashStringHelper *vistaECPHome::statusText(int statusCode)
      {
        switch (statusCode)
        {

        case 100:
          return F("ZMedical");
        case 101:
          return F("ZPersonal Emergency");
        case 102:
          return F("ZFail to report in");
        case 110:
          return F("ZFire");
        case 111:
          return F("ZSmoke");
        case 112:
          return F("ZCombustion");
        case 113:
          return F("ZWater Flow");
        case 114:
          return F("ZHeat");
        case 115:
          return F("ZPull Station");
        case 116:
          return F("ZDuct");
        case 117:
          return F("ZFlame");
        case 118:
          return F("ZNear Alarm");
        case 120:
          return F("ZPanic");
        case 121:
          return F("UDuress");
        case 122:
          return F("ZSilent");
        case 123:
          return F("ZAudible");
        case 124:
          return F("ZDuress – Access granted");
        case 125:
          return F("ZDuress – Egress granted");
        case 126:
          return F("UHoldup suspicion print");
        case 127:
          return F("URemote Silent Panic");
        case 129:
          return F("ZPanic Verifier");
        case 130:
          return F("ZBurglary");
        case 131:
          return F("ZPerimeter");
        case 132:
          return F("ZInterior");
        case 133:
          return F("Z24 Hour (Safe)");
        case 134:
          return F("ZEntry/Exit");
        case 135:
          return F("ZDay/Night");
        case 136:
          return F("ZOutdoor");
        case 137:
          return F("ZTamper");
        case 138:
          return F("ZNear alarm");
        case 139:
          return F("ZIntrusion Verifier");
        case 140:
          return F("ZGeneral Alarm");
        case 141:
          return F("ZPolling loop open");
        case 142:
          return F("ZPolling loop short");
        case 143:
          return F("ZExpansion module failure");
        case 144:
          return F("ZSensor tamper");
        case 145:
          return F("ZExpansion module tamper");
        case 146:
          return F("ZSilent Burglary");
        case 147:
          return F("ZSensor Supervision Failure");
        case 150:
          return F("Z24 Hour NonBurglary");
        case 151:
          return F("ZGas detected");
        case 152:
          return F("ZRefrigeration");
        case 153:
          return F("ZLoss of heat");
        case 154:
          return F("ZWater Leakage");

        case 155:
          return F("ZFoil Break");
        case 156:
          return F("ZDay Trouble");
        case 157:
          return F("ZLow bottled gas level");
        case 158:
          return F("ZHigh temp");
        case 159:
          return F("ZLow temp");
        case 160:
          return F("ZAwareness Zone Response");
        case 161:
          return F("ZLoss of air flow");
        case 162:
          return F("ZCarbon Monoxide detected");
        case 163:
          return F("ZTank level");
        case 168:
          return F("ZHigh Humidity");
        case 169:
          return F("ZLow Humidity");
        case 200:
          return F("ZFire Supervisory");
        case 201:
          return F("ZLow water pressure");
        case 202:
          return F("ZLow CO2");
        case 203:
          return F("ZGate valve sensor");
        case 204:
          return F("ZLow water level");
        case 205:
          return F("ZPump activated");
        case 206:
          return F("ZPump failure");
        case 300:
          return F("ZSystem Trouble");
        case 301:
          return F("ZAC Loss");
        case 302:
          return F("ZLow system battery");
        case 303:
          return F("ZRAM Checksum bad");
        case 304:
          return F("ZROM checksum bad");
        case 305:
          return F("ZSystem reset");
        case 306:
          return F("ZPanel programming changed");
        case 307:
          return F("ZSelftest failure");
        case 308:
          return F("ZSystem shutdown");
        case 309:
          return F("ZBattery test failure");
        case 310:
          return F("ZGround fault");
        case 311:
          return F("ZBattery Missing/Dead");
        case 312:
          return F("ZPower Supply Overcurrent");
        case 313:
          return F("UEngineer Reset");
        case 314:
          return F("ZPrimary Power Supply Failure");

        case 315:
          return F("ZSystem Trouble");
        case 316:
          return F("ZSystem Tamper");

        case 317:
          return F("ZControl Panel System Tamper");
        case 320:
          return F("ZSounder/Relay");
        case 321:
          return F("ZBell 1");
        case 322:
          return F("ZBell 2");
        case 323:
          return F("ZAlarm relay");
        case 324:
          return F("ZTrouble relay");
        case 325:
          return F("ZReversing relay");
        case 326:
          return F("ZNotification Appliance Ckt. # 3");
        case 327:
          return F("ZNotification Appliance Ckt. #4");
        case 330:
          return F("ZSystem Peripheral trouble");
        case 331:
          return F("ZPolling loop open");
        case 332:
          return F("ZPolling loop short");
        case 333:
          return F("ZExpansion module failure");
        case 334:
          return F("ZRepeater failure");
        case 335:
          return F("ZLocal printer out of paper");
        case 336:
          return F("ZLocal printer failure");
        case 337:
          return F("ZExp. Module DC Loss");
        case 338:
          return F("ZExp. Module Low Batt.");
        case 339:
          return F("ZExp. Module Reset");
        case 341:
          return F("ZExp. Module Tamper");
        case 342:
          return F("ZExp. Module AC Loss");
        case 343:
          return F("ZExp. Module selftest fail");
        case 344:
          return F("ZRF Receiver Jam Detect");

        case 345:
          return F("ZAES Encryption disabled/ enabled");
        case 350:
          return F("ZCommunication  trouble");
        case 351:
          return F("ZTelco 1 fault");
        case 352:
          return F("ZTelco 2 fault");
        case 353:
          return F("ZLong Range Radio xmitter fault");
        case 354:
          return F("ZFailure to communicate event");
        case 355:
          return F("ZLoss of Radio supervision");
        case 356:
          return F("ZLoss of central polling");
        case 357:
          return F("ZLong Range Radio VSWR problem");
        case 358:
          return F("ZPeriodic Comm Test Fail /Restore");

        case 359:
          return F("Z");

        case 360:
          return F("ZNew Registration");
        case 361:
          return F("ZAuthorized  Substitution Registration");
        case 362:
          return F("ZUnauthorized  Substitution Registration");
        case 365:
          return F("ZModule Firmware Update Start/Finish");
        case 366:
          return F("ZModule Firmware Update Failed");

        case 370:
          return F("ZProtection loop");
        case 371:
          return F("ZProtection loop open");
        case 372:
          return F("ZProtection loop short");
        case 373:
          return F("ZFire trouble");
        case 374:
          return F("ZExit error alarm (zone)");
        case 375:
          return F("ZPanic zone trouble");
        case 376:
          return F("ZHoldup zone trouble");
        case 377:
          return F("ZSwinger Trouble");
        case 378:
          return F("ZCrosszone Trouble");

        case 380:
          return F("ZSensor trouble");
        case 381:
          return F("ZLoss of supervision  RF");
        case 382:
          return F("ZLoss of supervision  RPM");
        case 383:
          return F("ZSensor tamper");
        case 384:
          return F("ZRF low battery");
        case 385:
          return F("ZSmoke detector Hi sensitivity");
        case 386:
          return F("ZSmoke detector Low sensitivity");
        case 387:
          return F("ZIntrusion detector Hi sensitivity");
        case 388:
          return F("ZIntrusion detector Low sensitivity");
        case 389:
          return F("ZSensor selftest failure");
        case 391:
          return F("ZSensor Watch trouble");
        case 392:
          return F("ZDrift Compensation Error");
        case 393:
          return F("ZMaintenance Alert");
        case 394:
          return F("ZCO Detector needs replacement");
        case 400:
          return F("UOpen/Close");
        case 401:
          return F("UArmed AWAY");
        case 402:
          return F("UGroup O/C");
        case 403:
          return F("UAutomatic O/C");
        case 404:
          return F("ULate to O/C (Note: use 453 or 454 instead )");
        case 405:
          return F("UDeferred O/C (Obsolete do not use )");
        case 406:
          return F("UCancel");
        case 407:
          return F("URemote arm/disarm");
        case 408:
          return F("UQuick arm");
        case 409:
          return F("UKeyswitch O/C");
        case 411:
          return F("UCallback request made");
        case 412:
          return F("USuccessful  download/access");
        case 413:
          return F("UUnsuccessful access");
        case 414:
          return F("USystem shutdown command received");
        case 415:
          return F("UDialer shutdown command received");

        case 416:
          return F("ZSuccessful Upload");
        case 418:
          return F("URemote Cancel");
        case 419:
          return F("URemote Verify");
        case 421:
          return F("UAccess denied");
        case 422:
          return F("UAccess report by user");
        case 423:
          return F("ZForced Access");
        case 424:
          return F("UEgress Denied");
        case 425:
          return F("UEgress Granted");
        case 426:
          return F("ZAccess Door propped open");
        case 427:
          return F("ZAccess point Door Status Monitor trouble");
        case 428:
          return F("ZAccess point Request To Exit trouble");
        case 429:
          return F("UAccess program mode entry");
        case 430:
          return F("UAccess program mode exit");
        case 431:
          return F("UAccess threat level change");
        case 432:
          return F("ZAccess relay/trigger fail");
        case 433:
          return F("ZAccess RTE shunt");
        case 434:
          return F("ZAccess DSM shunt");
        case 435:
          return F("USecond Person Access");
        case 436:
          return F("UIrregular Access");
        case 441:
          return F("UArmed STAY");
        case 442:
          return F("UKeyswitch Armed STAY");
        case 443:
          return F("UArmed with System Trouble Override");
        case 450:
          return F("UException O/C");
        case 451:
          return F("UEarly O/C");
        case 452:
          return F("ULate O/C");
        case 453:
          return F("UFailed to Open");
        case 454:
          return F("UFailed to Close");
        case 455:
          return F("UAutoarm Failed");
        case 456:
          return F("UPartial Arm");
        case 457:
          return F("UExit Error (user)");
        case 458:
          return F("UUser on Premises");
        case 459:
          return F("URecent Close");
        case 461:
          return F("ZWrong Code Entry");
        case 462:
          return F("ULegal Code Entry");
        case 463:
          return F("URearm after Alarm");
        case 464:
          return F("UAutoarm Time Extended");
        case 465:
          return F("ZPanic Alarm Reset");
        case 466:
          return F("UService On/Off Premises");

        case 501:
          return F("ZAccess reader disable");
        case 520:
          return F("ZSounder/Relay  Disable");
        case 521:
          return F("ZBell 1 disable");
        case 522:
          return F("ZBell 2 disable");
        case 523:
          return F("ZAlarm relay disable");
        case 524:
          return F("ZTrouble relay disable");
        case 525:
          return F("ZReversing relay disable");
        case 526:
          return F("ZNotification Appliance Ckt. # 3 disable");
        case 527:
          return F("ZNotification Appliance Ckt. # 4 disable");
        case 531:
          return F("ZModule Added");
        case 532:
          return F("ZModule Removed");
        case 551:
          return F("ZDialer disabled");
        case 552:
          return F("ZRadio transmitter disabled");
        case 553:
          return F("ZRemote  Upload/Download disabled");
        case 570:
          return F("ZZone/Sensor bypass");
        case 571:
          return F("ZFire bypass");
        case 572:
          return F("Z24 Hour zone bypass");
        case 573:
          return F("ZBurg. Bypass");
        case 574:
          return F("UGroup bypass");
        case 575:
          return F("ZSwinger bypass");
        case 576:
          return F("ZAccess zone shunt");
        case 577:
          return F("ZAccess point bypass");
        case 578:
          return F("ZVault Bypass");
        case 579:
          return F("ZVent Zone Bypass");
        case 601:
          return F("ZManual trigger test report");
        case 602:
          return F("ZPeriodic test report");
        case 603:
          return F("ZPeriodic RF transmission");
        case 604:
          return F("UFire test");
        case 605:
          return F("ZStatus report to follow");
        case 606:
          return F("ZListenin to follow");
        case 607:
          return F("UWalk test mode");
        case 608:
          return F("ZPeriodic test  System Trouble Present");
        case 609:
          return F("ZVideo Xmitter active");
        case 611:
          return F("ZPoint tested OK");
        case 612:
          return F("ZPoint not tested");
        case 613:
          return F("ZIntrusion Zone Walk Tested");
        case 614:
          return F("ZFire Zone Walk Tested");
        case 615:
          return F("ZPanic Zone Walk Tested");
        case 616:
          return F("ZService Request");
        case 621:
          return F("ZEvent Log reset");
        case 622:
          return F("ZEvent Log 50% full");
        case 623:
          return F("ZEvent Log 90% full");
        case 624:
          return F("ZEvent Log overflow");
        case 625:
          return F("UTime/Date reset");
        case 626:
          return F("ZTime/Date inaccurate");
        case 627:
          return F("ZProgram mode entry");

        case 628:
          return F("ZProgram mode exit");
        case 629:
          return F("Z32 Hour Event log marker");
        case 630:
          return F("ZSchedule change");
        case 631:
          return F("ZException schedule change");
        case 632:
          return F("ZAccess schedule change");
        case 641:
          return F("ZSenior Watch Trouble");
        case 642:
          return F("ULatchkey Supervision");
        case 643:
          return F("ZRestricted Door Opened");
        case 645:
          return F("ZHelp Arrived");
        case 646:
          return F("ZAddition Help Needed");
        case 647:
          return F("ZAddition Help Cancel");
        case 651:
          return F("ZReserved for Ademco Use");
        case 652:
          return F("UReserved for Ademco Use");
        case 653:
          return F("UReserved for Ademco Use");
        case 654:
          return F("ZSystem Inactivity");
        case 655:
          return F("UUser Code X modified by Installer");
        case 703:
          return F("ZAuxiliary #3");
        case 704:
          return F("ZInstaller Test");
        case 750:
          return F("ZUser Assigned");
        case 751:
          return F("ZUser Assigned");
        case 752:
          return F("ZUser Assigned");
        case 753:
          return F("ZUser Assigned");
        case 754:
          return F("ZUser Assigned");
        case 755:
          return F("ZUser Assigned");
        case 756:
          return F("ZUser Assigned");
        case 757:
          return F("ZUser Assigned");
        case 758:
          return F("ZUser Assigned");
        case 759:
          return F("ZUser Assigned");
        case 760:
          return F("ZUser Assigned");
        case 761:
          return F("ZUser Assigned");
        case 762:
          return F("ZUser Assigned");
        case 763:
          return F("ZUser Assigned");
        case 764:
          return F("ZUser Assigned");
        case 765:
          return F("ZUser Assigned");
        case 766:
          return F("ZUser Assigned");
        case 767:
          return F("ZUser Assigned");
        case 768:
          return F("ZUser Assigned");
        case 769:
          return F("ZUser Assigned");
        case 770:
          return F("ZUser Assigned");
        case 771:
          return F("ZUser Assigned");
        case 772:
          return F("ZUser Assigned");
        case 773:
          return F("ZUser Assigned");
        case 774:
          return F("ZUser Assigned");
        case 775:
          return F("ZUser Assigned");
        case 776:
          return F("ZUser Assigned");
        case 777:
          return F("ZUser Assigned");
        case 778:
          return F("ZUser Assigned");
        case 779:
          return F("ZUser Assigned");
        case 780:
          return F("ZUser Assigned");
        case 781:
          return F("ZUser Assigned");
        case 782:
          return F("ZUser Assigned");
        case 783:
          return F("ZUser Assigned");
        case 784:
          return F("ZUser Assigned");
        case 785:
          return F("ZUser Assigned");
        case 786:
          return F("ZUser Assigned");
        case 787:
          return F("ZUser Assigned");
        case 788:
          return F("ZUser Assigned");
        case 789:
          return F("ZUser Assigned");

        case 796:
          return F("ZUnable to output signal (Derived Channel)");
        case 798:
          return F("ZSTU Controller down (Derived Channel)");
        case 900:
          return F("ZDownload Abort");
        case 901:
          return F("ZDownload Start/End");
        case 902:
          return F("ZDownload Interrupted");
        case 903:
          return F("ZDevice Flash Update Started/ Completed");
        case 904:
          return F("ZDevice Flash Update Failed");
        case 910:
          return F("ZAutoclose with Bypass");
        case 911:
          return F("ZBypass Closing");
        case 912:
          return F("ZFire Alarm Silence");
        case 913:
          return F("USupervisory Point test Start/End");
        case 914:
          return F("UHoldup test Start/End");
        case 915:
          return F("UBurg. Test Print Start/End");
        case 916:
          return F("USupervisory Test Print Start/End");
        case 917:
          return F("ZBurg. Diagnostics Start/End");
        case 918:
          return F("ZFire Diagnostics Start/End");
        case 919:
          return F("ZUntyped diagnostics");
        case 920:
          return F("UTrouble Closing (closed with burg. during exit)");
        case 921:
          return F("UAccess Denied Code Unknown");
        case 922:
          return F("ZSupervisory Point Alarm");
        case 923:
          return F("ZSupervisory Point Bypass");
        case 924:
          return F("ZSupervisory Point Trouble");
        case 925:
          return F("ZHoldup Point Bypass");
        case 926:
          return F("ZAC Failure for 4 hours");
        case 927:
          return F("ZOutput Trouble");
        case 928:
          return F("UUser code for event");
        case 929:
          return F("ULogoff");
        case 954:
          return F("ZCS Connection Failure");
        case 961:
          return F("ZRcvr Database Connection Fail/Restore");
        case 962:
          return F("ZLicense Expiration Notify");
        case 999:
          return F("Z1 and 1/3 Day No Read Log");
        default:
          return F("ZUnknown");
        }
      }



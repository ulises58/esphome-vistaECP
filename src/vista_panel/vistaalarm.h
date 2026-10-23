#pragma once

#include "../vista_ecp/vista.h"
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "paneltext.h"
#include "Regexp.h"

#define KP_ADDR 17 // only used as a default if not set in the yaml
#define MAX_ZONES 48
#define MAX_PARTITIONS 3
#define DEFAULTPARTITION 1

#define SFIRE "fire"
#define SALARM "alm"
#define STROUBLE "trbl"
#define SARMEDSTAY "arms"
#define SARMEDAWAY "arma"
#define SINSTANT "armi"
#define SREADY "rdy"
#define SAC "ac"
#define SBYPASS "byp"
#define SCHIME "chm"
#define SBAT "bat"
#define SARMEDNIGHT "armn"
#define SARMED "arm"
#define SSYSTEMSTATE "ss"
#define SLRR "lrr"
#define SRF "rf"
#define SLINE1 "ln1"
#define SLINE2 "ln2"
#define SBEEP "bp"
#define SZONESTATUS "zs"
#define SZONE "z"
#define SRELAY "r"

#define WIRED_TYPE 0
#define RF_TYPE 1
#define LOOP_TYPE 2

#define ASYNC_CORE 1
// default pins to use for serial comms to the panel
// The pinouts below are only examples. You can choose any other gpio pin that is available and not needed for boot.
// These have proven to work fine.
// ESP32. Ejemplos del proyecto original.
#define RX_PIN 22
#define TX_PIN 21
#define MONITOR_PIN 18


extern Vista vista;

    enum sysState
    {
      sarmedaway,
      sarmedstay,
      sarmednight,
      sdisarmed,
      striggered,
      sunavailable,
    };

    enum reqStates
    {
      rsidle,
      rsopenzones,
      rsbypasszones,
      rszonecount,
      rspartitionlist,
      rspartitionid,
      rszoneinfo,
      rsicode,
      rsdate,
    };

      struct serialType
      {
        uint16_t zone;
        int mask;
      };

class vistaECPHome
{
#define FC(s) ((const char*)(s))

    public:
      vistaECPHome(char kpaddr = KP_ADDR, int receivePin = RX_PIN, int transmitPin = TX_PIN, int monitorTxPin = MONITOR_PIN, int maxzones = MAX_ZONES, int maxpartitions = MAX_PARTITIONS, bool invertrx = true, bool inverttx = true, bool invertmon = true, uint8_t inputrx = INPUT, uint8_t inputmon = INPUT);
      ~vistaECPHome();
      void set_accessCode(const char *ac) { _accessCode = ac; }
      void set_rfSerialLookup(const char *rf) { _rfSerialLookup = rf; }
      void set_quickArm(bool qa) { _quickArm = qa; }
      void set_lrrSupervisor(bool ls) { _lrrSupervisor = ls; }
      void set_auiaddr(uint8_t addr) { 
        _auiAddr = addr; 
        switch (addr)
        {
          case 1: _auiAddrMask=0x02;break;
          case 2: _auiAddrMask=0x04;break;
          case 5: _auiAddrMask=0x20;break;
          case 6: _auiAddrMask=0x40;break;
          default: _auiAddr=0;_auiAddrMask=0;
        }};
      void set_expanderAddr(uint8_t addr)
      {
        if (!addr) return;
        vista.addModule(addr);
      }
      void set_rf_emulation(bool emulate) {
        vista.set_rf_emulation(emulate);
      }
      void set_rf_addr(uint8_t addr){
        vista.set_rf_addr(addr);
        vista.addModule(addr);
      }
      void set_maxZones(int mz) { _maxZones = mz; }
      void set_partitionKeypad(uint8_t idx, uint8_t addr)
      {
        if (idx && idx < 4)
          _partitionKeypads[idx] = addr;
      }
      void set_defaultPartition(uint8_t dp) { _defaultPartition = dp; }
      void set_debug(uint8_t db) { _debug = db; }
      void set_ttl(uint32_t t) { TTL = t; };
      void set_text(uint8_t text_idx, const char *text)
      {
        switch (text_idx)
        {
        case 1:
          FAULT = text;
          break;
        case 2:
          BYPAS = text;
          break;
        case 3:
          ALARM = text;
          break;
        case 4:
          FIRE = text;
          break;
        case 5:
          CHECK = text;
          break;
        case 6:
          TRBL = text;
          break;
        case 7:
          HITSTAR = text;
          break;
        default:
          break;
        }
      }
      void publishBinaryState(const std::string &cstr, uint8_t partition, bool open);
      void publishTextState(const std::string &cstr, uint8_t partition, std::string *text);

      void stop();


      void begin();

      void alarm_disarm(std::string code, int32_t partition);

      void alarm_arm_home(int32_t partition);

      void alarm_arm_night(int32_t partition);

      void alarm_arm_away(int32_t partition);

      void alarm_trigger_fire(std::string code, int32_t partition);

      void alarm_trigger_panic(std::string code, int32_t partition);

      void set_zone_fault(int32_t zone, bool fault);

      void set_keypad_address(int32_t addr)
      {
        // if (addr > 0 and addr < 30)
        ///  vista.setKpAddr(addr); //disabled for now
      }

      void alarm_keypress(std::string keystring);

      void alarm_keypress_partition(std::string keystring, int32_t partition);
      void send_cmd_bytes(int32_t addr, std::string hexbytes);
      void setDefaultKpAddr(uint8_t p);
      void set_alarm_state(std::string const &state, std::string code = "", int partition = DEFAULTPARTITION);
        void loop();
     void disconnectVista()
      {
        vista.stop();
      }
      bool connected()
      {
        vista.keybusConnected = vista.connected;
        return vista.connected;
      }

      void setExpFault(int zone, bool fault)
      {
        vista.setExpFault(zone, fault);
      }


     bool forceRefreshGlobal, forceRefreshZones;

      void setBinarySensorCallback(std::function<void(const std::string &cstr, uint8_t partition, bool open)> callback)
      {
        binarySensorCallback = callback;
      }
      void setTextSensorCallback(std::function<void(const std::string &cstr, uint8_t partition, std::string *text)> callback)
      {
        textSensorCallback = callback;
      }

    private:
      struct sensorObjType
      {
        uint16_t zone;
        unsigned long time;
        uint8_t partition : 7;
        uint8_t open : 1;
        uint8_t bypass : 1;
        uint8_t alarm : 1;
        uint8_t check : 1;
        uint8_t fire : 1;
        uint8_t panic : 1;
        uint8_t trouble : 1;
        uint8_t lowbat : 1;
        uint8_t active : 1;
        bool rflowbat ;
        bool external;
        uint32_t serial;
        uint8_t loopmask;
        uint8_t type; 
        bool emulated;
        bool is_binary;
        uint32_t hash;
        const char * id_type;
     };

      std::function<void(const std::string &, uint8_t, std::string *)> textSensorCallback;
      std::function<void(const std::string &, uint8_t, bool)> binarySensorCallback;

      void publishZoneStatus(sensorObjType *zt, const char *open)
      {
        if (zt == NULL)
          return;
        {
          std::string ztxt(open);
          publishTextState(SZONE, zt->zone, &ztxt);
        }
      }

         void publishZoneStatus(sensorObjType *zt,bool open)
      {
        if (zt == NULL)
          return;
        publishBinaryState(SZONE, zt->zone, open);
      }

      void publishStatus(const char *sensor, bool open, uint8_t partition = 0)
      {
        publishBinaryState(sensor, partition, open);
      }

      void publishSystemStatus(std::string statusCode, uint8_t partition)
      {
        publishTextState(SSYSTEMSTATE, partition, &statusCode);
      }

      void publishLrrMsg(std::string msg)
      {
        publishTextState(SLRR, 0, &msg);
      }
      void publishRfMsg(std::string msg)
      {
        publishTextState(SRF, 0, &msg);
      }
      void publishLine1(std::string msg, uint8_t partition)
      {
        publishTextState(SLINE1, partition, &msg);
      }
      void publishLine2(std::string msg, uint8_t partition)
      {
        publishTextState(SLINE2, partition, &msg);
      }
      void publishBeeps(uint8_t beep, uint8_t partition)
      {
        std::string beeps=std::to_string(beep);
        publishTextState(SBEEP, partition, &beeps);
      }
      void publishZoneExtendedStatus(std::string msg)
      {
        publishTextState(SZONESTATUS, 0, &msg);
      }
      void publishRelayStatus(uint8_t addr, int channel, bool state)
      {
        std::string sensor = SRELAY + std::to_string(addr) + std::to_string(channel);
        publishBinaryState(sensor, 0, state);
      }
      uint8_t getLoopMask(uint8_t loop, uint8_t type)
      {
        if (type==RF_TYPE) {
          switch (loop)
          {
            case 1:
              return 0x80;
            case 2:
              return 0x20;
            case 3:
              return 0x10;
            case 4:
              return 0x40;
            default:
              return 0x80;
            }
      } else 
          return type; //temp for LOOP_TYPE
      }

      int TTL = 30000;
      uint8_t _debug = 0;
      char _keypadAddr1 = 0;
      int _rxPin = 0;
      int _txPin = 0;
      int _monitorPin = 0;
      int _maxZones = 0;
      int _maxPartitions = 0;
      bool _invertRx;
      bool _invertTx;
      bool _invertMon;
      uint8_t _inputRx = 0;
      uint8_t _inputMon = 0;
      uint8_t _auiAddr = 0;
      uint8_t _auiAddrMask=0;
      // bool activeAuiAddr=false;
      bool sendAuiTime();
      // bool sendAuiTime(int year, int month, int day, int hour, int minute,int seconds,int dow);
      char _auiSeq = 8;
      void processAuiQueue();
      // int8_t dateReqStatus=0;
      const char *_accessCode;
      const char *_rfSerialLookup;
      bool _quickArm;

      bool _lrrSupervisor;
      char *_partitionKeypads;
      int _defaultPartition = DEFAULTPARTITION;
      uint8_t *_partitions;
      std::string _topicPrefix, _topic;

      std::string _previousZoneStatusMsg;

      uint8_t _partitionTargets;
      unsigned long _lowBatteryTime;

      bool  _forceRefresh;

      sysState _currentSystemState;
   
      struct auiCmdType
      {
        reqStates state = rsidle;
        unsigned long time = 0;
        uint8_t partition = 0;
        uint8_t records = 0;
        uint8_t record = 0;
        bool pending = false;
      };


      sensorObjType sensorObjType_INIT = {
          .zone = 0,
          .time = 0,
          .partition = 0,
          .open = 0,
          .bypass = 0,
          .alarm = 0,
          .check = 0,
          .fire = 0,
          .panic = 0,
          .trouble = 0,
          .lowbat = 0,
          .active = 0,
          .serial = 0,
          .loopmask = 0x80,
          .type=0,
          .emulated=0,
          .is_binary=0,
          .hash=0,
          .id_type="",

        };

      struct
      {
        uint8_t bell : 1;
        uint8_t wrx1 : 1;
        uint8_t wrx2 : 1;
        uint8_t loop : 1;
        uint8_t duress : 1;
        uint8_t panic1 : 1;
        uint8_t panic2 : 1;
        uint8_t panic3 : 1;
      } otherSup;


      struct alarmStatusType
      {
        unsigned long time;
        bool state;
        uint16_t zone;
        char prompt[17];
      };


      alarmStatusType * fireStatus;
      alarmStatusType * panicStatus;
      alarmStatusType * alarmStatus;

      // struct lrrType
      // {
      //   int code;
      //   uint8_t qual;
      //   uint16_t zone;
      //   uint8_t user;
      // };

      struct lightStates
      {
        uint8_t away : 1;
        uint8_t stay : 1;
        uint8_t night : 1;
        uint8_t instant : 1;
        uint8_t bypass : 1;
        uint8_t ready : 1;
        uint8_t ac : 1;
        uint8_t chime : 1;
        uint8_t bat : 1;
        uint8_t alarm : 1;
        uint8_t check : 1;
        uint8_t fire : 1;
        uint8_t canceled : 1;
        uint8_t trouble : 1;
        uint8_t armed : 1;
      };

      lightStates currentLightState,
          previousLightState;
      // enum lrrtype
      // {
      //   user_t,
      //   zone_t
      // };


      struct partitionStateType
      {
        sysState previousSystemState;
        lightStates previousLightState;
        int lastbeeps;
        bool refreshStatus;
        bool refreshLights;
        bool active;
      };

     partitionStateType partitionStates_INIT = {
          .previousSystemState= sunavailable,
          .previousLightState={0,0,0,0,0,0,0,0,0,0,0,0,0,0},
          .lastbeeps=0,
          .refreshStatus=false,
          .refreshLights=false,
          .active=false,
      };

      void updateZoneState(sensorObjType *zt, int p, bool state, unsigned long t);
      char *parseAUIMessage(char *cmd);
      void processZoneList(char *list);
      void sendZoneRequest();
      void getRFSerial(sensorObjType *zt);
     // void enableModuleAddr(sensorObjType n);

       partitionStateType *partitionStates;

 
      struct cmdQueueItem * vistaCmd;
      TaskHandle_t xHandle{NULL};
      static void cmdQueueTask(void *args);
     // static void setupTask(void *args);
      
      void createZone(uint16_t z, uint8_t p = 0);

      auiCmdType auiCmd;
      std::vector<sensorObjType> extZones{};
      std::queue<auiCmdType> auiQueue{};


      sensorObjType *getZone(uint16_t z);
      sensorObjType *getZoneFromSerial(uint32_t serialCode);
      sensorObjType *getSensorObj(const char *id_type);
      void zoneStatusUpdate(sensorObjType *zt);
      void assignPartitionToZone(sensorObjType *zt);


        bool isInt(std::string s, int base);

        long int toInt(std::string s, int base);

        bool areEqual(char *a1, char *a2, uint8_t len);

        int getZoneFromPrompt(char *p1);
       // void getNameFromPrompt(char *p1, char *p2,std::string & out);
        void getZoneName(uint16_t zone, std::string & out,bool append=false);
 
        // bool promptContains(char * p1, const char * msg, int & zone);

        void printPacket(const char *label, char cbuf[], int len);

        //  std::string getF7Lookup(char cbuf[]) ;

        void updateDisplayLines(uint8_t partition);

        int getZoneFromChannel(uint8_t deviceAddress, uint8_t channel);

        //   void translatePrompt(char * cbuf) ;

        void getPartitionsFromMask();


        const __FlashStringHelper *statusText(int statusCode);


      };

extern vistaECPHome *alarmPanelPtr;

#include "AppData.h"
#include <string.h>

// ===== Live status =====
int16_t statusTempC = 12;
int16_t statusVinV  = 12;
// Fan telemetry (provide these from your measurement code)
int16_t fan1Current_mA = 12;
int16_t fan2Current_mA = 12;
int16_t fan1Power_W = 12;
int16_t fan2Power_W = 12;
int16_t fan1Run_m = 12;
int16_t fan2Run_m = 12;

// Alarms (shown in menu; not actually committed as settings)
bool alarmDoor=true, alarmWater=false, alarmSmoke=true, alarmTemp=false, alarmFanFault=true, alarmAviation = false;

// ===== Live settings defaults =====
Settings gLive = {
  // Fan thresholds
  .tempThrL = 24,
  .tempThrH = 35,
  .tempHighThr = 45,
  .togglePeriodMs = 1,

  // profiles / model
  .fanProfile = PROF_AUTO,
  .fan1Model   = MODEL_KRUBO,
  .fan2Model   = MODEL_KRUBO,

  .fan1nominal = 250,
  .fan2nominal = 250,

  // units
  .fanCurrentUnit = UNIT_mA,

  // system
  .voltLThrV = 10,
  .voltHighThrV = 15,

  .LDRThreshold = 100,
  // modbus
  .baudrate = 115200,
  .slaveID = 1
};

// ===== Staged settings (what the menu edits) =====
Settings gStage = gLive;

// ====== staging helpers ======
static inline bool settingsEqual(const Settings& a, const Settings& b) {
  return memcmp(&a, &b, sizeof(Settings)) == 0;
}

void stageBegin()   { gStage = gLive; }
bool settingsDirty(){ return !settingsEqual(gStage, gLive); }
void stageApply()   { gLive  = gStage; }
void stageDiscard() { gStage = gLive;  }

// ===== Demo arrays + timing =====
static int Temperature[] = {27, 30, 32};
static int Voltage[]     = {27, 30, 32};
static int Fan1Current[] = {27, 30, 32};
static int Fan2Current[] = {27, 30, 32};
static int Fan1Power[] = {27, 30, 32};
static int Fan2Power[] = {27, 30, 32};
static int Fan1Run[] = {27, 30, 32};
static int Fan2Run[] = {27, 30, 32};
static uint8_t demoIdx = 0;
static unsigned long lastUpdate = 0;
static const unsigned long UPDATE_MS = 5000; // 5s

void demoDataInit() {
  lastUpdate = 0;
  demoIdx = 0;
}

void demoDataTick(unsigned long now) {
  if (now - lastUpdate < UPDATE_MS) return;
  lastUpdate = now;

  statusTempC = Temperature[demoIdx];
  statusVinV  = Voltage[demoIdx];
  fan1Current_mA = Fan1Current[demoIdx];
  fan2Current_mA = Fan2Current[demoIdx];
  fan1Power_W = Fan1Power[demoIdx];
  fan2Power_W = Fan2Power[demoIdx];
  fan1Run_m = Fan1Run[demoIdx];
  fan2Run_m = Fan2Run[demoIdx];

  // Flip alarms each tick (visual demo)
  alarmDoor     = !alarmDoor;
  alarmWater    = !alarmWater;
  alarmSmoke    = !alarmSmoke;
  alarmTemp     = !alarmTemp;
  alarmFanFault = !alarmFanFault;
  alarmAviation = !alarmAviation;

  demoIdx = (uint8_t)((demoIdx + 1) % 3);
}

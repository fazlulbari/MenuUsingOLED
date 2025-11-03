#pragma once
#include <stdint.h>

// ===== Live status (read-only in menu) =====
extern float statusTempC;
extern float statusVinV;

// Fan telemetry (provide these from your measurement code)
extern float fan1Current_mA;
extern float fan2Current_mA;
extern float fan1Power_W;
extern float fan2Power_W;
extern float fan1Run_m;
extern float fan2Run_m;
extern bool Light_Condition;

// Alarms (shown in menu but not committed; you can keep them read-only in logic)
extern bool alarmDoor, alarmWater, alarmSmoke, alarmTemp, alarmFanFault, alarmAviation;

// ===== Settings schema =====
enum { PROF_AUTO, PROF_NORMAL };
enum { MODEL_KRUBO, MODEL_DELTA, MODEL_CUSTOM };
enum { UNIT_mA, UNIT_A };

struct Settings {
  // Fan settings
  int16_t tempThrL;
  int16_t tempThrH;
  int16_t tempHighThr;
  uint16_t togglePeriodMs;

  int fanProfile;
  int fan1Model;
  int fan2Model;

  int fan1nominal;
  int fan2nominal;


  int fanCurrentUnit;

  // System
  int16_t voltLThrV;
  int16_t voltHighThrV;

  int16_t LDRThreshold;

  // Modbus
  long baudrate;
  uint8_t slaveID;
};

// ===== Live settings (used by firmware logic) =====
extern Settings gLive;

// ===== Staged settings (bound to the menu) =====
extern Settings gStage;

// ===== Staging API =====
void stageBegin();            // copy live -> stage (call when entering menu)
bool settingsDirty();         // compare stage vs live
void stageApply();            // copy stage -> live
void stageDiscard();          // copy live -> stage (revert)

// ===== Demo data tick (your 5s updates for status + alarms) =====
void demoDataInit();
void demoDataTick(unsigned long now);

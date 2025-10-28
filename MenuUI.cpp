#include "MenuUI.h"

#include <menu.h>
#include <menuIO/u8g2Out.h>
#include <menuIO/serialIn.h>
#include <menuIO/serialOut.h>
#include <menuIO/chainStream.h>
#include <OneButton.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "AppData.h"
#include "Pins.h"
#include "FanAnimator.h"
#include "images.h"

using namespace Menu;

// ===== Display / fonts =====
#define fontName u8g2_font_5x8_tf   // keep for your custom screens
#define fontX 7                      // wider cells for submenu
#define fontY 12                     // taller cells for submenu
#define offsetX 0
#define offsetY 0
#define U8_Width 128
#define U8_Height 64

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, RESET_PIN, OLED_ADDR);

// ===== Menu colors =====
static const colorDef<uint8_t> colors[6] MEMMODE={
  {{0,0},{0,1,1}},
  {{1,1},{1,0,0}},
  {{1,1},{1,0,0}},
  {{1,1},{1,0,0}},
  {{0,1},{0,0,1}},
  {{1,1},{1,0,0}},
};

// ===== Password / unlock =====
static bool passwordVisible=false;
static uint8_t passDigits[4]={0,0,0,0};
static uint8_t passIndex=0;
static const uint8_t PASSWORD[4]={1,0,0,1};
static bool passWrong=false;
// Keep Settings unlocked until we return to Idle
static bool settingsUnlocked=false;

// ===== UI state =====
static volatile UIMode uiMode=UI_IDLE;
static unsigned long lastInputMs=0;
static const unsigned long MENU_TIMEOUT_MS=60000UL;

// ===== Horizontal main menu =====
static const char* MAIN_LABELS[4] = {"Status","Alarms","Settings","About"};
static uint8_t mainIdx = 0;
static const uint8_t MAIN_COUNT = 4;

static const uint8_t WIN_SIZE = 2;
static uint8_t winStart = 0;       // index of left tile in the window

// ----- Idle page cycling -----
static uint8_t idleCaseIndex = 0; 

static inline void ensureWindow(){
  // keep mainIdx visible inside [winStart, winStart+WIN_SIZE-1]
  if(mainIdx < winStart) winStart = mainIdx;
  else if(mainIdx >= winStart + WIN_SIZE) winStart = mainIdx - (WIN_SIZE - 1);

  // clamp for safety
  if(winStart + WIN_SIZE > MAIN_COUNT) {
    winStart = (MAIN_COUNT >= WIN_SIZE) ? (MAIN_COUNT - WIN_SIZE) : 0;
  }
}


// Accel for long-press digit nav
static unsigned long digitHoldStartMsUp=0, digitHoldStartMsDown=0;
static unsigned long lastDigitStepMs=0;
static inline uint16_t accelInterval(unsigned long heldMs){
  if (heldMs>2500) return 30;
  if (heldMs>1600) return 50;
  if (heldMs>1000) return 80;
  if (heldMs>600)  return 120;
  return 160;
}

// Exit confirmation
static bool confirmVisible=false;
static uint8_t confirmIdx=0;

// Fans
extern const uint8_t* images[4];
static FanAnimator fans(u8g2);

// ===== Button queue → ArduinoMenu input bridge =====
static const uint8_t BTN_Q_SIZE=8;
static volatile uint8_t qHead=0,qTail=0;
static char btnQueue[BTN_Q_SIZE];


class ButtonsIn:public menuIn{
public:
  int available() override { return qHead==qTail?0:1; }
  int peek() override { return qHead==qTail?-1:(int)btnQueue[qTail]; }
  int read() override {
    if (qHead==qTail) return -1;
    char c=btnQueue[qTail];
    qTail=(uint8_t)((qTail+1)%BTN_Q_SIZE);
    return (int)c;
  }
  void flush() override { while(qHead!=qTail) qTail=(uint8_t)((qTail+1)%BTN_Q_SIZE); }
  size_t write(uint8_t) override { return 1; }
} btnInput;

static inline void pushCmd(char c){
  uint8_t next=(uint8_t)((qHead+1)%BTN_Q_SIZE);
  if (next!=qTail){ btnQueue[qHead]=c; qHead=next; }
}

// ===== Buttons =====
static OneButton btnUp(BTN_UP,true,true);
static OneButton btnDown(BTN_DOWN,true,true);
static OneButton btnEnter(BTN_ENTER,true,true);
static OneButton btnEsc(BTN_ESC,true,true);

// ===== Menu geometry =====
#define MAX_DEPTH 6
#define MENU_PX_W 160
#define MENU_COLS (MENU_PX_W / fontX)  // auto from fontX (now 7)
#define MENU_ROWS 5                    // fixed: 5 rows in submenu

// Render throttle
static const uint16_t FRAME_MS=25;
static unsigned long lastFrameMs=0;

// ===== Forward decls =====
static result doFactoryReset(eventMask, prompt&);
static result onEnterSettings(eventMask, prompt&);
// Forward-declare goIdle so handlers can call it before nav exists
static inline void goIdle();

// ===== Helpers =====
static void passReset(){ passDigits[0]=passDigits[1]=passDigits[2]=passDigits[3]=0; passIndex=0; passWrong=false; }
static bool passIsCorrect(){ for(int i=0;i<4;i++) if(passDigits[i]!=PASSWORD[i]) return false; return true; }

#ifndef ROFIELD
#define ROFIELD(target,label,units,low,high,step,tune) \
  FIELD_(__COUNTER__, Menu::menuField, ((Menu::systemStyles)(Menu::_parentDraw)), \
         target, label, units, low, high, step, tune, doNothing, noEvent, noStyle)
#endif

// ===== Menus =====
MENU(MenuStatus,"Status",doNothing,noEvent,noStyle
  ,ROFIELD(statusTempC,"Temperature","C",-40,125,1,0)
  ,ROFIELD(statusVinV,"InputVoltage","V",0,300,1,0)
  ,EXIT("<Back")
)

MENU(MenuAlarms,"Alarms",doNothing,noEvent,noStyle
  ,ROFIELD(alarmDoor,"DoorAlarm"," ",0,1,1,0)
  ,ROFIELD(alarmWater,"WaterAlarm"," ",0,1,1,0)
  ,ROFIELD(alarmSmoke,"SmokeAlarm"," ",0,1,1,0)
  ,ROFIELD(alarmTemp,"TempAlarm"," ",0,1,1,0)
  ,ROFIELD(alarmFanFault,"FanFaultAlarm"," ",0,1,1,0)
  ,ROFIELD(alarmAviation,"AviationAlarm"," ",0,1,1,0)
  ,EXIT("<Back")
)

MENU(MenuTempSettings,"TemperatureSettings",doNothing,noEvent,noStyle
  ,FIELD(gStage.tempThrL,"TempThresLOW","C",-40,125,1,0,doNothing,noEvent,noStyle)
  ,FIELD(gStage.tempThrH,"TempThreHIGH","C",-40,125,1,0,doNothing,noEvent,noStyle)
  ,FIELD(gStage.tempHighThr,"TempHIGHThres","C",-40,125,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

MENU(MenuSystemSettings,"SystemSettings",doNothing,noEvent,noStyle
  ,FIELD(gStage.voltLThrV,"VoltLOWThres","V",0,300,1,0,doNothing,noEvent,noStyle)
  ,FIELD(gStage.voltHighThrV,"VoltHIGHThres","V",0,300,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

SELECT(gStage.fanCurrentUnit, MenuFanCurrentUnit,"FanCurrentUnit",doNothing,noEvent,noStyle
  ,VALUE("mA",UNIT_mA,doNothing,noEvent)
  ,VALUE("A", UNIT_A, doNothing,noEvent)
);

SELECT(gStage.fanProfile, MenuFanProfile,"FanProfile",doNothing,noEvent,noStyle
  ,VALUE("Auto",PROF_AUTO,doNothing,noEvent)
  ,VALUE("Normal",PROF_NORMAL,doNothing,noEvent)
);

SELECT(gStage.fan1Model, MenuFan1Model,"Fan1Model",doNothing,noEvent,noStyle
  ,VALUE("KRUBO",MODEL_KRUBO,doNothing,noEvent)
  ,VALUE("DELTA",MODEL_DELTA,doNothing,noEvent)
  ,VALUE("CUSTOM",MODEL_CUSTOM,doNothing,noEvent)
);

MENU(Fan1Settings,"Fan 1 Settings",doNothing,noEvent,noStyle
  ,SUBMENU(MenuFan1Model)
  ,FIELD(gStage.fan1nominal,"NomFan1Curr","mA",0,10000,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

SELECT(gStage.fan2Model, MenuFan2Model,"Fan2Model",doNothing,noEvent,noStyle
  ,VALUE("KRUBO",MODEL_KRUBO,doNothing,noEvent)
  ,VALUE("DELTA",MODEL_DELTA,doNothing,noEvent)
  ,VALUE("CUSTOM",MODEL_CUSTOM,doNothing,noEvent)
);

MENU(Fan2Settings,"Fan 2 Settings",doNothing,noEvent,noStyle
  ,SUBMENU(MenuFan2Model)
  ,FIELD(gStage.fan2nominal,"NomFan2Curr","mA",0,10000,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

MENU(MenuFanSettings,"FanSettings",doNothing,noEvent,noStyle
  ,FIELD(gStage.togglePeriodMs,"TogglePeriod","min",0,100,1,0,doNothing,noEvent,noStyle)
  ,SUBMENU(MenuFanCurrentUnit)
  ,SUBMENU(MenuFanProfile)
  ,SUBMENU(Fan1Settings)
  ,SUBMENU(Fan2Settings)
  ,EXIT("<Back")
);

SELECT(gStage.baudrate, MenuBaudrate,"Baudrate",doNothing,noEvent,noStyle
  ,VALUE("9600",9600,doNothing,noEvent)
  ,VALUE("19200",19200,doNothing,noEvent)
  ,VALUE("38400",38400,doNothing,noEvent)
  ,VALUE("57600",57600,doNothing,noEvent)
  ,VALUE("115200",115200,doNothing,noEvent)
);

MENU(MenuModbusSettings,"ModbusSettings",doNothing,noEvent,noStyle
  ,SUBMENU(MenuBaudrate)
  ,FIELD(gStage.slaveID,"SlaveID","",1,247,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

MENU(MenuAviationSettings,"AviationSettings",doNothing,noEvent,noStyle
  ,FIELD(gStage.LDRThreshold,"AviLDRThres","LUX",1,247,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

// Gate Settings with password unless unlocked
static result onEnterSettings(eventMask, prompt&){
  if(!settingsUnlocked){
    passwordVisible=true; passReset();
    return quit;
  }
  return proceed;
}

MENU(MenuSettings,"Settings",onEnterSettings,enterEvent,noStyle
  ,SUBMENU(MenuTempSettings)
  ,SUBMENU(MenuSystemSettings)
  ,SUBMENU(MenuFanSettings)
  ,SUBMENU(MenuModbusSettings)
  ,SUBMENU(MenuAviationSettings)
  ,OP("Run Factory Reset",doFactoryReset,enterEvent)
  ,EXIT("<Back")
);

MENU(MenuAbout,"About",doNothing,noEvent,noStyle
  ,OP("SWVersion: 1.14",doNothing,noEvent)
  ,OP("SWDate: 2025-04-01",doNothing,noEvent)
  ,OP("Installed: 2025-04-01",doNothing,noEvent)
  ,OP("Serial: SARBS_ODCC_1001",doNothing,noEvent)
  ,EXIT("<Back")
);

MENU(mainMenu,"Main",doNothing,noEvent,noStyle
  ,SUBMENU(MenuStatus)
  ,SUBMENU(MenuAlarms)
  ,SUBMENU(MenuSettings)
  ,SUBMENU(MenuAbout)
);

// ===== IO glue + NAVROOT =====
// static serialIn serial(Serial);
MENU_INPUTS(in,&btnInput)
MENU_OUTPUTS(out,MAX_DEPTH
  ,U8G2_OUT(u8g2,colors,fontX,fontY,offsetX,offsetY,{0,0,MENU_COLS,MENU_ROWS})
  ,SERIAL_OUT(Serial)
);
NAVROOT(nav,mainMenu,MAX_DEPTH,in,out);

static bool atRoot(){ return nav.level==0; }

// Now that nav exists, define goIdle
static inline void goIdle(){
  confirmVisible=false;
  passwordVisible=false;
  uiMode=UI_IDLE;
  btnInput.flush();
  nav.reset();
  settingsUnlocked=false;
  mainIdx = 0;
}


static void openMainFromIndex(uint8_t idx){
  if (idx >= MAIN_COUNT) idx = MAIN_COUNT - 1;

  uiMode = UI_MENU;
  btnInput.flush();

  // Start clean at root
  nav.reset();

  // Make absolutely sure we're at the first root item (Status)
  // Send several UPs so even if wrap is on or selection is weird, we end up at top.
  for (uint8_t k = 0; k < 8; ++k) {
    nav.doNav(upCmd);
  }

  // Now step down to the desired tile
  for (uint8_t k = 0; k < idx; ++k) {
    nav.doNav(downCmd);
  }

  // Enter the selected submenu
  nav.doNav(enterCmd);
}

static void drawMainMenuHorizontal(){
  // Title
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(1);
  u8g2.drawStr(2,9,"Main");

  ensureWindow();

  // Layout for exactly 2 tiles centered on 128x64
  const int tileW = 56;      // wider for image+label
  const int tileH = 40;      // taller bar
  const int gap   = 6;       // spacing between tiles
  const int totalW = 2*tileW + gap;
  const int startX = (U8_Width - totalW)/2; // centered
  const int topY   = 14;                     // below the title

  for(uint8_t lane=0; lane<WIN_SIZE; ++lane){
    uint8_t item = winStart + lane;
    if(item >= MAIN_COUNT) break;

    int x = startX + lane*(tileW+gap);
    int y = topY;

    bool selected = (item == mainIdx);

    // Tile
    if(selected){
      u8g2.drawBox(x,y,tileW,tileH);    // filled when selected
      u8g2.setDrawColor(0);
    } else {
      u8g2.drawFrame(x,y,tileW,tileH);  // outline when not selected
      u8g2.setDrawColor(1);
    }

    // --- [ICON SLOT] ---
    // Use 24x24 px XBM icons here
    {
      const int iconW = 24, iconH = 24;
      const int ix = x + (tileW - iconW)/2;
      const int iy = y + 6;

      // choose per-tile icon
      const uint8_t* bmp = ICON_STATUS_24;
      switch (item) {
        case 0: bmp = ICON_STATUS_24;   break; // "Status"
        case 1: bmp = ICON_ALARMS_24;   break; // "Alarms"
        case 2: bmp = ICON_SETTINGS_24; break; // "Settings"
        case 3: bmp = ICON_ABOUT_24;    break; // "About"
      }

      // Draw one bitmap for both states:
      // - Not selected: draw color = 1 → white on black
      // - Selected:     we filled tile (white) and set draw color = 0 → black on white
      u8g2.drawXBMP(ix, iy, iconW, iconH, bmp);
    }

    // Label at bottom of tile
    {
      const char* lbl = MAIN_LABELS[item];

      // Make sure the same font is active for measuring & drawing
      u8g2.setFont(u8g2_font_5x7_tr);

      int w  = u8g2.getStrWidth(lbl);           // pixel width of the label
      int tx = x + (tileW - w) / 2;             // center within the tile
      int ty = y + tileH - 2;                   // baseline near bottom

      u8g2.drawStr(tx, ty, lbl);
    }

    // restore draw color if it was inverted
    if(selected) u8g2.setDrawColor(1);
  }

  // Optional left/right indicators
  if(winStart > 0)                   u8g2.drawTriangle(2, 32, 6, 28, 6, 36);          // left arrow
  if(winStart+WIN_SIZE<MAIN_COUNT)   u8g2.drawTriangle(126, 32, 122,28,122,36);       // right arrow
}




// ===== Drawing helpers =====
static void drawIdleScreen(){
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(1);
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  u8g2.drawStr(2,9,"SARBS ODCC Plus Status");
  char line[24];

  switch (idleCaseIndex) {
    case 0: // Temp / Vin
      snprintf(line, sizeof(line), "Temp: %d C", statusTempC);
      u8g2.drawStr(2,20,line);
      snprintf(line, sizeof(line), "Vin : %d V", statusVinV);
      u8g2.drawStr(2,30,line);
      break;

    case 1: // F1C / F2C (mA)
      snprintf(line, sizeof(line), "F1C: %dmA", fan1Current_mA);
      u8g2.drawStr(2,20,line);
      snprintf(line, sizeof(line), "F2C: %dmA", fan2Current_mA);
      u8g2.drawStr(2,30,line);
      break;

    case 2: // F1P / F2P (W)
      snprintf(line, sizeof(line), "F1P: %dW", fan1Power_W);
      u8g2.drawStr(2,20,line);
      snprintf(line, sizeof(line), "F2P: %dW", fan2Power_W);
      u8g2.drawStr(2,30,line);
      break;
  }

  snprintf(line,sizeof(line),"F1: %dM",fan1Run_m); u8g2.drawStr(80,35,line); 
  snprintf(line,sizeof(line),"F2: %dM",fan2Run_m); u8g2.drawStr(80,65,line);

  // if(alarmDoor == 1){     u8g2.drawXBMP(2, 36, 16, 16, ICON_DOOR_16);  }
  // if(alarmWater == 1){    u8g2.drawXBMP(26, 36, 16, 16, ICON_WATER_16); }
  // if(alarmSmoke == 1){    u8g2.drawXBMP(50, 36, 16, 16, ICON_SMOKE_16); }
  // if(alarmTemp == 1){     u8g2.drawXBMP(14, 49, 16, 16, ICON_FIRE_16); }
  // if(alarmFanFault == 1){ u8g2.drawXBMP(38, 49, 16, 16, ICON_FAN_16);  }
  // if(alarmAviation == 1){ u8g2.drawXBMP(62, 49, 16, 16, ICON_LIGHT_16); }
  u8g2.drawXBMP(0, 36, 16, 16, ICON_DOOR_16);  
  u8g2.drawXBMP(24, 36, 16, 16, ICON_WATER_16);
  u8g2.drawXBMP(48, 36, 16, 16, ICON_SMOKE_16);
  u8g2.drawXBMP(14, 48, 16, 16, ICON_FIRE_16); 
  u8g2.drawXBMP(36, 49, 16, 16, ICON_FAN_16);  
  u8g2.drawXBMP(60, 49, 16, 16, ICON_LIGHT_16);
  fans.draw();
}

static void drawConfirmDialog(){
  u8g2.setDrawColor(0); u8g2.drawBox(0,0,U8_Width,U8_Height);
  u8g2.setDrawColor(1);
  const int w=114,h=48,x=(U8_Width-w)/2,y=(U8_Height-h)/2;
  u8g2.drawFrame(x,y,w,h);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(x+8,y+12,"Apply changes?");
  const char* opts[3]={"Apply & Exit","Discard & Exit","Cancel"};
  for(int k=0;k<3;++k){
    int yy=y+24+k*10;
    if(confirmIdx==k){
      u8g2.setDrawColor(1); u8g2.drawBox(x+6,yy-7,w-12,9);
      u8g2.setDrawColor(0); u8g2.drawStr(x+10,yy,opts[k]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(x+10,yy,opts[k]);
    }
  }
}

static void drawPasswordDialog(){
  u8g2.setDrawColor(0); u8g2.drawBox(0,0,U8_Width,U8_Height);
  u8g2.setDrawColor(1);
  const int w=118,h=46,x=(U8_Width-w)/2,y=(U8_Height-h)/2;
  u8g2.drawFrame(x,y,w,h);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(x+22,y+11,"Enter Password");
  const int boxW=18, boxH=14, gap=6, totalW=4*boxW+3*gap;
  const int startX=x+(w-totalW)/2, baseY=y+20;
  for(int i=0;i<4;i++){
    int bx=startX+i*(boxW+gap);
    if(i==passIndex){
      u8g2.drawBox(bx,baseY,boxW,boxH);
      u8g2.setDrawColor(0); char d[2]; d[0]='0'+passDigits[i]; d[1]=0; u8g2.drawStr(bx+(boxW/2-3),baseY+10,d);
      u8g2.setDrawColor(1); u8g2.drawFrame(bx,baseY,boxW,boxH);
    } else {
      u8g2.drawFrame(bx,baseY,boxW,boxH);
      char d[2]; d[0]='0'+passDigits[i]; d[1]=0; u8g2.drawStr(bx+(boxW/2-3),baseY+10,d);
    }
  }
  u8g2.setFont(u8g2_font_4x6_tr);
  if(passWrong) u8g2.drawStr(x+10,y+h-4,"Wrong Password. Try again");
  else          u8g2.drawStr(x+4,y+h-4,"Up/Dn=edit ENT=next ESC=prev");
}

// ===== Actions =====
static result doFactoryReset(eventMask, prompt&){
  Serial.println("[FactoryReset] Requested!");
  return proceed;
}

// ===== Buttons =====
static void setupButtonHandlers(){
  btnUp.attachClick([](){
    lastInputMs=millis();
    if(passwordVisible){ passWrong=false; passDigits[passIndex]=(uint8_t)((passDigits[passIndex]+1)%10); return; }
    if(confirmVisible){ if(confirmIdx>0) confirmIdx--; return; }

    if(!passwordVisible && !confirmVisible && uiMode==UI_MENU && atRoot()){
      if(mainIdx + 1 < MAIN_COUNT){
        mainIdx++;            // LEFT
      }
      return;
    }

    // If we're in idle screen, cycle the idle case pages
    if (uiMode == UI_IDLE) {
      idleCaseIndex = (uint8_t)((idleCaseIndex + 1) % 3);
      return;                 // don't pass to menu nav when idle
    }

    if(uiMode==UI_MENU||uiMode==UI_SUBMENU) pushCmd(defaultNavCodes[upCmd].ch);
  });

  // Down: move right at root AND tell ArduinoMenu to move down too
  btnDown.attachClick([](){
    lastInputMs=millis();
    if(passwordVisible){ passWrong=false; passDigits[passIndex]=(uint8_t)((passDigits[passIndex]+9)%10); return; }
    if(confirmVisible){ if(confirmIdx<2) confirmIdx++; return; }

    if(!passwordVisible && !confirmVisible && uiMode==UI_MENU && atRoot()){
      if(mainIdx > 0){
        mainIdx--;            // RIGHT
      }
      return;
    }

    // If we're in idle screen, cycle the idle case pages
    if (uiMode == UI_IDLE) {
      idleCaseIndex = (uint8_t)((idleCaseIndex +3 - 1) % 3);
      return;                 // don't pass to menu nav when idle
    }

    if(uiMode==UI_MENU||uiMode==UI_SUBMENU) pushCmd(defaultNavCodes[downCmd].ch);
  });

  // Enter: at root, just forward ENTER (nav already points to same item)
  btnEnter.attachClick([](){
    lastInputMs=millis();
    if(passwordVisible){ passWrong=false; passIndex=(uint8_t)((passIndex+1)%4); return; }
    if(confirmVisible){
      if(confirmIdx==0) stageApply();
      if(confirmIdx==0||confirmIdx==1){ goIdle(); return; }
      confirmVisible=false; return;
    }

    if(!passwordVisible && !confirmVisible && uiMode==UI_MENU && atRoot()){
      openMainFromIndex(MAIN_COUNT-mainIdx-1);   // open the tile the user sees
      return;
    }


    if(uiMode==UI_MENU||uiMode==UI_SUBMENU) pushCmd(defaultNavCodes[enterCmd].ch);
  });


  btnEsc.attachClick([](){
    lastInputMs=millis();
    if(passwordVisible){ passWrong=false; passIndex=(uint8_t)((passIndex+3)%4); return; }
    if(confirmVisible){ confirmVisible=false; return; }
    if(!atRoot()) pushCmd(defaultNavCodes[escCmd].ch);
  });

  // Double ENTER
  btnEnter.attachDoubleClick([](){
    lastInputMs=millis();
    if(passwordVisible){
      if(passIsCorrect()){
        settingsUnlocked=true;               // keep unlocked until Idle
        passwordVisible=false; passWrong=false; passReset();
        pushCmd(defaultNavCodes[enterCmd].ch);   // immediately enter Settings
      } else {
        settingsUnlocked=false;
        passWrong=true;
      }
      return;
    }
    confirmVisible=false; uiMode=UI_MENU; stageBegin();
  });

  // Double ESC
  btnEsc.attachDoubleClick([](){
    lastInputMs=millis();
    if(passwordVisible){ passwordVisible=false; passReset(); passWrong=false; uiMode=UI_MENU; return; }
    if(uiMode==UI_MENU||uiMode==UI_SUBMENU){
      if(settingsDirty()){ confirmVisible=true; confirmIdx=0; }
      else { goIdle(); } // relock on Idle
    }
  });

  auto sendRightIfEditing=[](){
    unsigned long now=millis(); lastInputMs=now;
    if(passwordVisible){
      unsigned long held=now-digitHoldStartMsUp; uint16_t step=accelInterval(held);
      if(now-lastDigitStepMs>=step){ passDigits[passIndex]=(uint8_t)((passDigits[passIndex]+1)%10); lastDigitStepMs=now; }
      return;
    }
    if(!confirmVisible && uiMode==UI_SUBMENU && nav.level>=2){
      unsigned long held=now-digitHoldStartMsUp; uint16_t step=accelInterval(held);
      if(now-lastDigitStepMs>=step){ pushCmd(defaultNavCodes[rightCmd].ch); lastDigitStepMs=now; }
    }
  };

  auto sendLeftIfEditing=[](){
    unsigned long now=millis(); lastInputMs=now;
    if(passwordVisible){
      unsigned long held=now-digitHoldStartMsDown; uint16_t step=accelInterval(held);
      if(now-lastDigitStepMs>=step){ passDigits[passIndex]=(uint8_t)((passDigits[passIndex]+9)%10); lastDigitStepMs=now; }
      return;
    }
    if(!confirmVisible && uiMode==UI_SUBMENU && nav.level>=2){
      unsigned long held=now-digitHoldStartMsDown; uint16_t step=accelInterval(held);
      if(now-lastDigitStepMs>=step){ pushCmd(defaultNavCodes[leftCmd].ch); lastDigitStepMs=now; }
    }
  };

  btnUp.attachLongPressStart([](){ digitHoldStartMsUp=millis(); lastDigitStepMs=0; lastInputMs=millis(); });
  btnDown.attachLongPressStart([](){ digitHoldStartMsDown=millis(); lastDigitStepMs=0; lastInputMs=millis(); });
  btnUp.attachDuringLongPress(sendRightIfEditing);
  btnDown.attachDuringLongPress(sendLeftIfEditing);

  btnUp.setClickTicks(60);     btnDown.setClickTicks(60);
  btnEnter.setClickTicks(220); btnEsc.setClickTicks(220);
  btnUp.setPressTicks(450);    btnDown.setPressTicks(450);
  btnUp.setDebounceTicks(2);   btnDown.setDebounceTicks(2);
  btnEnter.setDebounceTicks(2); btnEsc.setDebounceTicks(2);
}

// ===== Public API =====
void uiSetup(){
  pinMode(BTN_UP,INPUT_PULLUP);
  pinMode(BTN_DOWN,INPUT_PULLUP);
  pinMode(BTN_ENTER,INPUT_PULLUP);
  pinMode(BTN_ESC,INPUT_PULLUP);
  setupButtonHandlers();

  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();
  u8g2.begin();
  u8g2.setFont(fontName);

  const uint16_t W=u8g2.getDisplayWidth();
  const uint16_t H=u8g2.getDisplayHeight();
  fans.addFan(W-16-26,H-16-38,images,4,16,16,50);
  fans.addFan(W-16-5, H-16-38,images,4,16,16,150);
  fans.addFan(W-16-26,H-16-10,images,4,16,16,450);
  fans.addFan(W-16-5, H-16-10,images,4,16,16,1350);

  lastInputMs=millis();
  lastFrameMs=0;
  lastDigitStepMs=0;
}

void uiLoop(){
  demoDataTick(millis());
  btnUp.tick(); btnDown.tick(); btnEnter.tick(); btnEsc.tick();

  uint32_t now = millis();
  if ((now / 5000) % 2 == 0) {
    fans.setFanSpeed(0, 50);   // first 5s = fast
  } else {
    fans.setFanSpeed(0, 800);  // next 5s = slow
  }

  if(uiMode==UI_IDLE) fans.update();

  if(!confirmVisible && !passwordVisible && (uiMode==UI_MENU || uiMode==UI_SUBMENU)){
    nav.doInput();                                // <-- IMPORTANT: always run
    uiMode = (nav.level==0) ? UI_MENU : UI_SUBMENU;

    if(millis()-lastInputMs > MENU_TIMEOUT_MS){
      stageDiscard();
      confirmVisible=false; passwordVisible=false;
      goIdle();
    }
  }

  unsigned long now1=millis();
  if(now1-lastFrameMs<FRAME_MS) return;
  lastFrameMs=now1;

  u8g2.firstPage();
  do{
    // inside the page loop:
    if(uiMode==UI_MENU || uiMode==UI_SUBMENU){
      if(confirmVisible)       drawConfirmDialog();
      else if(passwordVisible) drawPasswordDialog();
      else {
        if(atRoot()){
            // Just draw your custom 2-tile carousel; don’t push nav commands here.
            u8g2.setDrawColor(1);
            u8g2.setFont(u8g2_font_5x7_tr);
            drawMainMenuHorizontal();
          }
        else {
          // normal submenus
          u8g2.setDrawColor(1);
          u8g2.setFont(u8g2_font_6x10_tf);
          nav.doOutput();
        }
      }
    } else {
      drawIdleScreen();
    }

  } while(u8g2.nextPage());
}

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
#define fontName u8g2_font_5x8_tf
#define fontX 6
#define fontY 10
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

// ===== Password dialog =====
static bool passwordVisible=false;
static uint8_t passDigits[4]={0,0,0,0};
static uint8_t passIndex=0;
static const uint8_t PASSWORD[4]={1,0,0,1};
static bool passWrong=false;
static bool passwordVerified=false;   // one-shot gate to enter Settings after success

// ===== UI state =====
static volatile UIMode uiMode=UI_IDLE;
static unsigned long lastInputMs=0;
static const unsigned long MENU_TIMEOUT_MS=60000UL;

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
#define MENU_PX_W 124
#define MENU_COLS (MENU_PX_W/6)        // menu uses a tighter font in draw
#define MENU_ROWS (U8_Height/10)

// Render throttle
static const uint16_t FRAME_MS=25;
static unsigned long lastFrameMs=0;

// ===== Forward decls =====
static result doFactoryReset(eventMask, prompt&);

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
  ,FIELD(gStage.tempThrL,"TempeThresholdL","C",-40,125,1,0,doNothing,noEvent,noStyle)
  ,FIELD(gStage.tempThrH,"TempThresholdH","C",-40,125,1,0,doNothing,noEvent,noStyle)
  ,FIELD(gStage.tempHighThr,"TempHiThreshold","C",-40,125,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

MENU(MenuSystemSettings,"SystemSettings",doNothing,noEvent,noStyle
  ,FIELD(gStage.voltLThrV,"VoltLThreshold","V",0,300,1,0,doNothing,noEvent,noStyle)
  ,FIELD(gStage.voltHighThrV,"VoltHThreshold","V",0,300,1,0,doNothing,noEvent,noStyle)
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
  ,FIELD(gStage.fan1nominal,"NomFan1Current","mA",0,10000,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

SELECT(gStage.fan2Model, MenuFan2Model,"Fan2Model",doNothing,noEvent,noStyle
  ,VALUE("KRUBO",MODEL_KRUBO,doNothing,noEvent)
  ,VALUE("DELTA",MODEL_DELTA,doNothing,noEvent)
  ,VALUE("CUSTOM",MODEL_CUSTOM,doNothing,noEvent)
);

MENU(Fan2Settings,"Fan 2 Settings",doNothing,noEvent,noStyle
  ,SUBMENU(MenuFan2Model)
  ,FIELD(gStage.fan2nominal,"NomFan2Current","mA",0,10000,1,0,doNothing,noEvent,noStyle)
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
  ,FIELD(gStage.LDRThreshold,"AviLDRThreshold","LUX",1,247,1,0,doNothing,noEvent,noStyle)
  ,EXIT("<Back")
);

static result onEnterSettings(eventMask, prompt&);
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

MENU(mainMenu,"Main",doNothing,noEvent,wrapStyle
  ,SUBMENU(MenuStatus)
  ,SUBMENU(MenuAlarms)
  ,SUBMENU(MenuSettings)
  ,SUBMENU(MenuAbout)
);

// ===== IO glue + NAVROOT =====
static serialIn serial(Serial);
MENU_INPUTS(in,&btnInput,&serial)
MENU_OUTPUTS(out,MAX_DEPTH
  ,U8G2_OUT(u8g2,colors,fontX,fontY,offsetX,offsetY,{0,0,MENU_COLS,MENU_ROWS})
  ,SERIAL_OUT(Serial)
);
NAVROOT(nav,mainMenu,MAX_DEPTH,in,out);

static bool atRoot(){ return nav.level==0; }

// ===== Drawing helpers =====
static void drawIdleScreen(){
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(1);
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  u8g2.drawStr(2,9,"SARBS ODCC Plus Status");
  char line[24];
  snprintf(line,sizeof(line),"Temp: %d C",statusTempC); u8g2.drawStr(2,20,line);
  snprintf(line,sizeof(line),"Vin : %d V",statusVinV);  u8g2.drawStr(2,30,line);
  if(alarmDoor == 1)
  {
  u8g2.drawXBMP(2, 55, 8, 8, ICON_DOOR_8);
  }
  if(alarmWater == 1)
  {
  u8g2.drawXBMP(14, 55, 8, 8, ICON_WATER_8);
  }
  if(alarmSmoke == 1)
  {
  u8g2.drawXBMP(26, 55, 8, 8, ICON_SMOKE_8);
  }
  if(alarmTemp == 1)
  {
  u8g2.drawXBMP(38, 55, 8, 8, ICON_FIRE_8);
  }
  if(alarmFanFault == 1)
  {
  u8g2.drawXBMP(50, 55, 8, 8, ICON_FAN_8);
  }
  if(alarmAviation == 1)
  {
  u8g2.drawXBMP(62, 55, 8, 8, ICON_LIGHT_8);
  }
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

// Password UI
static void passReset(){ passDigits[0]=passDigits[1]=passDigits[2]=passDigits[3]=0; passIndex=0; passWrong=false; }
static bool passIsCorrect(){ for(int i=0;i<4;i++) if(passDigits[i]!=PASSWORD[i]) return false; return true; }

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

// ===== Menu enter guard for Settings =====
static result onEnterSettings(eventMask, prompt&){
  if(!passwordVerified){
    passwordVisible=true; passReset();
    return quit;
  }
  passwordVerified=false;   // one-shot allow
  return proceed;
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
    if(uiMode==UI_MENU||uiMode==UI_SUBMENU) pushCmd(defaultNavCodes[upCmd].ch);
  });

  btnDown.attachClick([](){
    lastInputMs=millis();
    if(passwordVisible){ passWrong=false; passDigits[passIndex]=(uint8_t)((passDigits[passIndex]+9)%10); return; }
    if(confirmVisible){ if(confirmIdx<2) confirmIdx++; return; }
    if(uiMode==UI_MENU||uiMode==UI_SUBMENU) pushCmd(defaultNavCodes[downCmd].ch);
  });

  btnEnter.attachClick([](){
    lastInputMs=millis();
    if(passwordVisible){ passWrong=false; passIndex=(uint8_t)((passIndex+1)%4); return; }
    if(confirmVisible){
      if(confirmIdx==0) stageApply();
      if(confirmIdx==0||confirmIdx==1){ confirmVisible=false; uiMode=UI_IDLE; btnInput.flush(); nav.reset(); return; }
      confirmVisible=false; return;
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
        passwordVerified=true;
        passwordVisible=false; passWrong=false; passReset();
        pushCmd(defaultNavCodes[enterCmd].ch);   // immediately enter Settings
      } else {
        passwordVerified=false; passWrong=true;
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
      else { confirmVisible=false; uiMode=UI_IDLE; btnInput.flush(); nav.reset(); }
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

  btnUp.setClickTicks(60);    btnDown.setClickTicks(60);
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
    fans.setFanSpeed(0, 800);   // next 5s = slow
  }

  if(uiMode==UI_IDLE) fans.update();

  if(!confirmVisible && !passwordVisible && (uiMode==UI_MENU || uiMode==UI_SUBMENU)){
    nav.doInput();
    uiMode=(nav.level==0)?UI_MENU:UI_SUBMENU;
    if(millis()-lastInputMs>MENU_TIMEOUT_MS){
      stageDiscard();
      confirmVisible=false; passwordVisible=false;
      uiMode=UI_IDLE; btnInput.flush(); nav.reset();
    }
  }

  unsigned long now1=millis();
  if(now1-lastFrameMs<FRAME_MS) return;
  lastFrameMs=now;

  u8g2.firstPage();
  do{
    if(uiMode==UI_MENU || uiMode==UI_SUBMENU){
      if(confirmVisible)      drawConfirmDialog();
      else if(passwordVisible)drawPasswordDialog();
      else {u8g2.setDrawColor(1);  u8g2.setFont(u8g2_font_5x7_tr); nav.doOutput(); }   // menu-only font
    } else {
      drawIdleScreen();
    }
  } while(u8g2.nextPage());
}

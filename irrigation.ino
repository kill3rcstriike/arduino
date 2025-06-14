#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ----- PIN DEFINITIONS -----
const int BTN_LEFT_PIN  = 2;   // back / prev
const int BTN_SEL_PIN   = 3;   // enter/toggle
const int BTN_RIGHT_PIN = 4;   // next

const int SERVO1_PIN    = 6;   // servo between rows 1-2
const int SERVO2_PIN    = 5;   // servo between rows 3-4

const int RELAY_VALVE1_PIN = 9;   // valve rows 1+2
const int RELAY_VALVE2_PIN = 8;   // valve rows 3+4
const int RELAY_PUMP1_PIN  = 13;  // tank pump
const int RELAY_PUMP2_PIN  = 12;  // irrigation pump

const int SOIL_PINS[4] = { A0, A1, A2, A3 };
const int TANK_DO_PIN  = 10;

// ----- MOISTURE THRESHOLDS -----
const int DRY_THRESHOLD = 600;   // reading above => dry
const int DELTA_WET     = 50;    // drop by this => watered
unsigned long pauseTime = 10;    // seconds between cycles
bool paramsConfigured   = false;

unsigned long cycleCount = 0;

// store baseline moisture at start of each cycle
int soilBaseline[4] = {0,0,0,0};

enum MenuLevel { ML_MAIN, ML_TEST, ML_PROG, ML_RUN };
MenuLevel menuLevel = ML_MAIN;

int mainIndex = 0;        // main menu index

const int TEST_ITEMS = 11;
int  testIndex     = 0;
bool inTestMode    = false;
bool valveState[2] = {false,false};
bool pumpState[2]  = {false,false};
bool servoState[2] = {false,false};

const int PROG_ITEMS = 3;
int  progIndex      = 0;
bool inServoSetup   = false;
bool inPauseSetup   = false;
int  servoSetupStep = 0;

bool inRunMode = false;

int lastL = HIGH, lastS = HIGH, lastR = HIGH;

LiquidCrystal_I2C lcd(0x27,16,2);
Servo servos[2];
int servoPos[2][2] = {{45,135},{45,135}};  // [servo][0=left,1=right]

const char* mainText[3] = {
  "1. Test Mode       ",
  "2. Program Params  ",
  "3. Run Program     "
};
const char* testText[TEST_ITEMS] = {
  "1. Soil1 Sensor   ","2. Soil2 Sensor   ","3. Soil3 Sensor   ","4. Soil4 Sensor   ",
  "5. Tank DO Sensor ","6. Servo1 Test    ","7. Servo2 Test    ",
  "8. Valve1 Test    ","9. Valve2 Test    ","10. Pump1 Test    ","11. Pump2 Test    "
};
const char* progText[PROG_ITEMS] = {
  "1. Set Servo Pos  ","2. Set Pause Time","3. Back           "
};

void updateBaselines(){
  for(int i=0;i<4;i++) soilBaseline[i] = analogRead(SOIL_PINS[i]);
}

void setup(){
  pinMode(BTN_LEFT_PIN,  INPUT_PULLUP);
  pinMode(BTN_SEL_PIN,   INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);

  pinMode(RELAY_VALVE1_PIN, OUTPUT); digitalWrite(RELAY_VALVE1_PIN, HIGH);
  pinMode(RELAY_VALVE2_PIN, OUTPUT); digitalWrite(RELAY_VALVE2_PIN, HIGH);
  pinMode(RELAY_PUMP1_PIN,  OUTPUT); digitalWrite(RELAY_PUMP1_PIN,  HIGH);
  pinMode(RELAY_PUMP2_PIN,  OUTPUT); digitalWrite(RELAY_PUMP2_PIN,  HIGH);

  servos[0].attach(SERVO1_PIN);
  servos[1].attach(SERVO2_PIN);
  servos[0].write(90);
  servos[1].write(90);

  pinMode(TANK_DO_PIN, INPUT);

  lcd.init();
  lcd.backlight();

  updateBaselines();
  showMainMenu();
}

void showMainMenu(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(mainText[mainIndex]);
  lcd.setCursor(0,1); lcd.print("< Back=\xE2\x86\x90  OK=2  Next=\xE2\x86\x92");
}

void showTestMenu(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(testText[testIndex]);
  lcd.setCursor(0,1); lcd.print("< Back=\xE2\x86\x90  OK=2  Next=\xE2\x86\x92");
}

void showProgMenu(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(progText[progIndex]);
  lcd.setCursor(0,1); lcd.print("< Back=\xE2\x86\x90  OK=2  Next=\xE2\x86\x92");
}

void showRunScreen(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Cycles: "); lcd.print(cycleCount);
  lcd.setCursor(0,1); lcd.print("< Back=\xE2\x86\x90         ");
}

void updateTestDisplay(){
  lcd.setCursor(0,1);
  if(testIndex<=4){
    int v = (testIndex<4? analogRead(SOIL_PINS[testIndex]) : digitalRead(TANK_DO_PIN));
    lcd.print("Val: "); lcd.print(v); lcd.print("    ");
  } else if(testIndex<=6){
    int idx=testIndex-5;
    int pos=servoState[idx]?180:0;
    lcd.print("Pos: "); lcd.print(pos); lcd.print("    ");
  } else if(testIndex<=8){
    int idx=testIndex-7;
    lcd.print(valveState[idx]?"Valve ON ":"Valve OFF"); lcd.print(" ");
  } else {
    int idx=testIndex-9;
    lcd.print(pumpState[idx]?"Pump ON  ":"Pump OFF "); lcd.print(" ");
  }
}

void toggleTestDevice(){
  if(testIndex==5 || testIndex==6){
    int idx=testIndex-5;
    servoState[idx]=!servoState[idx];
    servos[idx].attach(idx==0?SERVO1_PIN:SERVO2_PIN);
    servos[idx].write(servoState[idx]?180:0);
    delay(500);
    servos[idx].detach();
  } else if(testIndex>6 && testIndex<=8){
    int idx=testIndex-7;
    valveState[idx]=!valveState[idx];
    digitalWrite(idx==0?RELAY_VALVE1_PIN:RELAY_VALVE2_PIN,
                 valveState[idx]?LOW:HIGH);
  } else if(testIndex>8){
    int idx=testIndex-9;
    pumpState[idx]=!pumpState[idx];
    digitalWrite(idx==0?RELAY_PUMP1_PIN:RELAY_PUMP2_PIN,
                 pumpState[idx]?LOW:HIGH);
  }
  updateTestDisplay();
}

// ------ servo setup with button hold acceleration ------
void showServoSetup(){
  int s=servoSetupStep/2, c=servoSetupStep%2;
  // attach only the servo being configured to avoid cross interference
  if(s==0){
    if(!servos[0].attached()) servos[0].attach(SERVO1_PIN);
    if(servos[1].attached()) servos[1].detach();
  } else {
    if(!servos[1].attached()) servos[1].attach(SERVO2_PIN);
    if(servos[0].attached()) servos[0].detach();
  }
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Set S"); lcd.print(s+1); lcd.print(c?" Ch2":" Ch1");
  lcd.setCursor(0,1);
  lcd.print("Pos: "); lcd.print(servoPos[s][c]); lcd.print("    ");
  servos[s].write(servoPos[s][c]);
}

void handleServoSetup(){
  static unsigned long holdStartL=0, holdStartR=0;
  static unsigned long lastStepL=0, lastStepR=0;
  static unsigned long selStart=0;
  static bool          selHeld=false;

  int s=servoSetupStep/2, c=servoSetupStep%2;
  int curL=digitalRead(BTN_LEFT_PIN), curR=digitalRead(BTN_RIGHT_PIN), curS=digitalRead(BTN_SEL_PIN);
  unsigned long now=millis();

  if(curL==LOW){
    if(lastL==HIGH){
      holdStartL=now; lastStepL=now;
      servoPos[s][c]=max(0,servoPos[s][c]-1); showServoSetup();
    } else if(now-lastStepL>(now-holdStartL>1500?50:(now-holdStartL>800?100:300))){
      servoPos[s][c]=max(0,servoPos[s][c]-1); showServoSetup(); lastStepL=now;
    }
  }
  if(curR==LOW){
    if(lastR==HIGH){
      holdStartR=now; lastStepR=now;
      servoPos[s][c]=min(180,servoPos[s][c]+1); showServoSetup();
    } else if(now-lastStepR>(now-holdStartR>1500?50:(now-holdStartR>800?100:300))){
      servoPos[s][c]=min(180,servoPos[s][c]+1); showServoSetup(); lastStepR=now;
    }
  }

  if(curS==LOW && lastS==HIGH){
    selStart=now; selHeld=true;
  } else if(curS==LOW && selHeld && now-selStart>3000){
    servoSetupStep++;
    if(servoSetupStep>=4){
      inServoSetup=false;
      paramsConfigured=true;
      servos[0].detach();
      servos[1].detach();
      showProgMenu();
    } else {
      showServoSetup();
    }
    selHeld=false;
  } else if(curS==HIGH && lastS==LOW){
    if(selHeld && now-selStart<3000){
      inServoSetup=false;
      servos[0].detach();
      servos[1].detach();
      showProgMenu();
    }
    selHeld=false;
  }

  lastL=curL; lastR=curR; lastS=curS;
}

void showPauseSetup(){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Set Pause(s)   ");
  lcd.setCursor(0,1); lcd.print(pauseTime); lcd.print("    ");
}

void handlePauseSetup(){
  static unsigned long holdStartL=0, holdStartR=0;
  static unsigned long lastStepL=0, lastStepR=0;
  static unsigned long selStart=0;
  static bool          selHeld=false;

  int curL=digitalRead(BTN_LEFT_PIN), curR=digitalRead(BTN_RIGHT_PIN), curS=digitalRead(BTN_SEL_PIN);
  unsigned long now=millis();

  if(curL==LOW){
    if(lastL==HIGH){
      holdStartL=now; lastStepL=now; pauseTime=max(1UL,pauseTime-1); showPauseSetup();
    } else if(now-lastStepL>(now-holdStartL>1500?50:(now-holdStartL>800?100:300))){
      pauseTime=max(1UL,pauseTime-1); showPauseSetup(); lastStepL=now;
    }
  }
  if(curR==LOW){
    if(lastR==HIGH){
      holdStartR=now; lastStepR=now; pauseTime++; showPauseSetup();
    } else if(now-lastStepR>(now-holdStartR>1500?50:(now-holdStartR>800?100:300))){
      pauseTime++; showPauseSetup(); lastStepR=now;
    }
  }

  if(curS==LOW && lastS==HIGH){
    selStart=now; selHeld=true;
  } else if(curS==LOW && selHeld && now-selStart>3000){
    inPauseSetup=false; paramsConfigured=true; showProgMenu();
    selHeld=false;
  } else if(curS==HIGH && lastS==LOW){
    if(selHeld && now-selStart<3000){
      inPauseSetup=false; showProgMenu();
    }
    selHeld=false;
  }
  lastL=curL; lastR=curR; lastS=curS;
}

// ---------- RUN MODE HELPERS ----------
bool pumpOn=false;

void ensurePumpOn(){
  if(!pumpOn){
    digitalWrite(RELAY_PUMP2_PIN, LOW);
    pumpOn=true;
  }
}

void ensurePumpOff(){
  if(pumpOn){
    digitalWrite(RELAY_PUMP2_PIN, HIGH);
    pumpOn=false;
  }
}

bool remainingNeeds(bool need[2][2]){
  for(int i=0;i<2;i++)
    for(int j=0;j<2;j++)
      if(need[i][j]) return true;
  return false;
}

void runCycle(){
  updateBaselines();

  bool need[2][2];
  for(int i=0;i<2;i++){
    int base=i*2;
    need[i][0]=soilBaseline[base]   > DRY_THRESHOLD;  // left
    need[i][1]=soilBaseline[base+1] > DRY_THRESHOLD;  // right
  }

  for(int i=0;i<2;i++){
    int valvePin=(i==0?RELAY_VALVE1_PIN:RELAY_VALVE2_PIN);
    int base=i*2;
    for(int side=1; side>=0; side--){            // right first
      if(!need[i][side]) continue;
      servos[i].attach(i==0?SERVO1_PIN:SERVO2_PIN);
      servos[i].write(servoPos[i][side]);
      delay(400);                    // allow servo to reach position
      digitalWrite(valvePin, LOW);   // open valve
      ensurePumpOn();
      unsigned long t0=millis();
      while(abs(analogRead(SOIL_PINS[base+side]) - soilBaseline[base+side]) < DELTA_WET && millis()-t0<10000){
        if(digitalRead(BTN_LEFT_PIN)==LOW){
          digitalWrite(valvePin, HIGH);
          ensurePumpOff();
          servos[i].detach();
          return;
        }
      }
      digitalWrite(valvePin, HIGH);  // close valve
      need[i][side]=false;
      if(!remainingNeeds(need)) ensurePumpOff();
      delay(500);                   // allow servo move time
    }
    servos[i].detach();
  }

  ensurePumpOff();
  delay(2000);

  for(unsigned long t=0;t<pauseTime*1000UL;t+=100){
    if(digitalRead(BTN_LEFT_PIN)==LOW) return;
    delay(100);
  }

  digitalWrite(RELAY_PUMP1_PIN, LOW);
  for(int t=0;t<10000;t+=100){
    if(digitalRead(BTN_LEFT_PIN)==LOW){
      digitalWrite(RELAY_PUMP1_PIN, HIGH);
      return;
    }
    delay(100);
  }
  digitalWrite(RELAY_PUMP1_PIN, HIGH);

  cycleCount++;
}

void handleRun(){
  showRunScreen();
  inRunMode=true;
  while(inRunMode){
    if(digitalRead(BTN_LEFT_PIN)==LOW){
      inRunMode=false; menuLevel=ML_MAIN; showMainMenu(); return;
    }
    runCycle();
    showRunScreen();
  }
}

void loop(){
  int curL=digitalRead(BTN_LEFT_PIN), curS=digitalRead(BTN_SEL_PIN), curR=digitalRead(BTN_RIGHT_PIN);

  switch(menuLevel){
    case ML_MAIN:
      if(curR==LOW && lastR==HIGH){ mainIndex=(mainIndex+1)%3; showMainMenu(); }
      if(curL==LOW && lastL==HIGH){ mainIndex=(mainIndex+2)%3; showMainMenu(); }
      if(curS==LOW && lastS==HIGH){
        if(mainIndex==2 && !paramsConfigured){
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Set Params First"); delay(1500); showMainMenu();
        } else {
          if(mainIndex==0){ menuLevel=ML_TEST; inTestMode=false; testIndex=0; showTestMenu(); }
          else if(mainIndex==1){ menuLevel=ML_PROG; inServoSetup=inPauseSetup=false; progIndex=0; showProgMenu(); }
          else { menuLevel=ML_RUN; handleRun(); }
        }
      }
      break;
    case ML_TEST:
      if(!inTestMode){
        if(curR==LOW && lastR==HIGH){ testIndex=(testIndex+1)%TEST_ITEMS; showTestMenu(); }
        if(curL==LOW && lastL==HIGH){ menuLevel=ML_MAIN; showMainMenu(); break; }
        if(curS==LOW && lastS==HIGH){ inTestMode=true; showTestMenu(); updateTestDisplay(); }
      } else {
    if(curL==LOW && lastL==HIGH){
        inTestMode=false;
        showTestMenu();
    } else {
        if(curS==LOW && lastS==HIGH){ toggleTestDevice(); }
        if(testIndex<=4) updateTestDisplay();
    }
      }
      break;
    case ML_PROG:
      if(inServoSetup) handleServoSetup();
      else if(inPauseSetup) handlePauseSetup();
      else {
        if(curR==LOW && lastR==HIGH){ progIndex=(progIndex+1)%PROG_ITEMS; showProgMenu(); }
        if(curL==LOW && lastL==HIGH){ menuLevel=ML_MAIN; showMainMenu(); break; }
        if(curS==LOW && lastS==HIGH){
          if(progIndex==0){
            inServoSetup=true; servoSetupStep=0; servos[0].attach(SERVO1_PIN); servos[1].attach(SERVO2_PIN);
            if(!paramsConfigured){
              servoPos[0][0]=servoPos[0][1]=90;
              servoPos[1][0]=servoPos[1][1]=90;
            }
            servos[0].write(servoPos[0][0]);
            servos[1].write(servoPos[1][0]);
            delay(500);
            showServoSetup();
          } else if(progIndex==1){
            inPauseSetup=true; showPauseSetup();
          } else {
            menuLevel=ML_MAIN; showMainMenu();
          }
        }
      }
      break;
    case ML_RUN:
      // handled in handleRun
      break;
  }

  lastL=curL; lastS=curS; lastR=curR;
  delay(50);
}

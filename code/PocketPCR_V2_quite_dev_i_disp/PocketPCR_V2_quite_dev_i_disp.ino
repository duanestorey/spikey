/*
PocketPCR PCR Thermocycler by GaudiLabs
Pocket size USB powered PCR Thermo Cycler

http://gaudi.ch/PocketPCR/

https://github.com/GaudiLabs/PocketPCR

*/

#include <math.h> 
#include "Adafruit_GFX.h"
#include <Adafruit_SSD1306.h>
#include "Rotary.h"
#include "FlashStorage.h"

#include <Fonts/FreeSans9pt7b.h>

const char VersionString[] = "Storey 0.0.1b";   // fork of GaudiLabs PocketPCR V1.02 2020
#define SPLASH_MS 2000                         // how long the start-up splash is shown

// These constants won't change. They're used to give names to the pins used:
const int analogInPin = A2;  // Analog input pin that the temperature sensor is attached to
const int fanPin = 4; // Analog output pin that the fan is attached to
const int heaterPin = 8; // Analog output pin that the LED is attached to
const int lidPin = 9; // Analog output pin that the LED is attached to
const int butPin = A4; // Analog output pin that the button is attached to

const int NTC_B = 3435;
const float NTC_TN = 298.15;
const int NTC_RN = 10000;
const float NTC_R0 = 4.7;

const float temperature_tollerance=0.5;
// Rotary encoder is wired with the common to ground and the two
// outputs to pins 6 and 7.
#define ENCODER_1 6
#define ENCODER_2 7
Rotary rotary = Rotary(ENCODER_1, ENCODER_2);

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels


// If using software SPI (the default case):
#define OLED_MOSI   20
#define OLED_CLK   21
#define OLED_DC    0 //11
#define OLED_CS    22 //12
#define OLED_RESET 5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
  OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
  
#define CASE_Main 1
#define CASE_Settings 2
#define CASE_EditSettings 3
#define CASE_Run 4
#define CASE_Done 5
#define CASE_Error 6

// Safety cutoff
#define ADC_MIN 20            // raw ADC at/below this: thermistor shorted
#define ADC_MAX 4075          // raw ADC at/above this: thermistor open / unplugged
#define T_MAX 105             // block over-temperature limit (°C)
#define NOHEAT_MS 60000UL     // full heater power this long ...
#define NOHEAT_RISE 2.0       // ... with less than this rise (°C) = heater/sensor not working
#define NOHEAT_BELOW 5.0      // only judged while block is this far below target (°C)
const char* errorReason = "";
unsigned long runStart = 0;       // millis() when the current run started (Elapsed Time)

// Time-left estimate: ramp rates (°C/s). Start from these guesses, then learn from each real ramp.
#define HEAT_RATE_DEFAULT 1.04   // measured on this device, 2026-11-07 (1-cycle run; refine with more data)
#define COOL_RATE_DEFAULT 1.01   // measured on this device, 2026-11-07 (1-cycle run; refine with more data)
#define RAMP_MIN_DELTA 3.0    // only ramps bigger than this (°C) are measured
float rampStartT = 0;
unsigned long rampStartMs = 0;
unsigned long noHeatStart = 0;
float noHeatTemp = 0;
bool noHeatTiming = false;

#define PCR_set 1
#define PCR_transition 2
#define PCR_time 3
#define PCR_end 4


#define PIDp 0.5
#define PIDi 0.0001
#define PIDd 0.15

#define SETTING_Size 6


String SETTING_String[SETTING_Size] = { "Init", "Denature", "Annealing" , "Extension", "Final" };
int SETTING_cycles = 20;

typedef struct {
int value[SETTING_Size*2];
} EEprom;

FlashStorage(my_flash_store, EEprom);

// Measured ramp statistics, kept in flash across power cycles (reset by a firmware upload).
// Saved once per run (when it ends) to spare the flash. Used for the time-left estimate and for tuning.
#define STATS_MAGIC 0x52414D31   // "RAM1": bump if the layout changes
typedef struct {
  uint32_t n;          // ramps measured into this step
  float meanRate;      // mean °C/s (running mean)
  float minRate, maxRate;
  float lastSecs;      // most recent ramp: seconds taken
  float lastDelta;     // most recent ramp: °C change (+ heating, - cooling)
} RampStep;
typedef struct {
  uint32_t magic;
  float heatRate, coolRate;      // overall blended rates (°C/s), fallback when a step has no data
  uint32_t rampsHeat, rampsCool;
  RampStep step[5];              // by target step: 0 Init, 1 Denature, 2 Annealing, 3 Extension, 4 Final
  uint32_t runsStarted, runsCompleted;
  uint32_t lastEstSecs;          // last completed run: estimate made at the start
  uint32_t lastActualSecs;       // last completed run: actual duration
} RampStats;
FlashStorage(stats_store, RampStats);
RampStats stats;
bool runCompleted = false;
// This run only (shown on the Done screen): ramps measured, total seconds and total °C moved
uint32_t runHeatN = 0, runCoolN = 0;
float runHeatSecs = 0, runCoolSecs = 0, runHeatDeg = 0, runCoolDeg = 0;
uint32_t runTotalSecs = 0;
uint32_t estAtStart = 0;
EEprom settings;
  

int sensorValue = 0;        // value read from the sesnor
int outputValue = 0;        // value output to the PWM (heater)
float temperature =0;
float temperature_mean =0;

float sensorResistance =0;
float sensorVoltage =0;

boolean editMode = false;
boolean minuteMode = false;

int counter = 0;        
int counter_save = 0;      

int x=0;
int caseUX = CASE_Main; 
int casePCR = PCR_set;
int MenuItem = 1;
int PCRstate = 0;
int PCRcycle = 1;

int PCRpwm = 0;

float TEMPset;
float TEMPdif;
float TEMPi;
bool PIDIntegration = false;
float TEMPcontrol;
float TEMPdif_a[10];
float TEMPd;
long TEMPclick=0;
long TIMEclick=0;
int TIMEcontrol=0;

int heatPower = 191;
int fanPower=0;

// Auto cool-down after a run: fan at full until the block temperature stops falling
#define COOL_SAMPLE_MS   30000UL   // check how far it has cooled every 30 s
#define COOL_STABLE_DROP 0.3       // stable when it drops less than this (°C) in one sample period
#define COOL_TIMEOUT_MS  60000UL   // fan runs at most 60 s after a run
bool coolInit = false;             // false = cool-down not started yet for this run
bool coolActive = false;
unsigned long coolStart = 0;
unsigned long coolSampleTime = 0;
float coolRef = 0;
void setup() {

  pinMode(ENCODER_1, INPUT_PULLUP);
  pinMode(ENCODER_2, INPUT_PULLUP);
  pinMode(butPin, INPUT_PULLUP);

  pinMode(fanPin, OUTPUT);
  pinMode(heaterPin, OUTPUT);
  pinMode(lidPin, OUTPUT);

  attachInterrupt(ENCODER_1, rotate, CHANGE);
  attachInterrupt(ENCODER_2, rotate, CHANGE);

  // initialize serial communications at 9600 bps:
  Serial.begin(115200);

  display.begin(SSD1306_SWITCHCAPVCC);
  
  display.clearDisplay();  
    
  display.setTextSize(1);
  display.setTextColor(WHITE); 
  // Splash: 5 lines (line 3 blank), small built-in font, centred on screen
  {
    const char* splash[] = { "PocketPCR", "by GaudiLabs", "", "Version", VersionString };
    const int n = 5, lineH = 10;
    int y = (SCREEN_HEIGHT - (n*lineH - 2)) / 2;   // -2: last line needs only its 8 px glyph height
    display.setFont();
    for (int i=0; i<n; i++) { drawCentered(y + i*lineH, splash[i]); }
  }
  display.dim(false);
  display.display();
  delay(SPLASH_MS);   // hold the button during the splash for left-handed mode


    if (!digitalRead(butPin)) {waitRelease(); display.setRotation(2);} else
    display.setRotation(0); // 0 for right handed, 2 for left handed

  // Show image buffer on the display hardware.
  


  analogReadResolution(12);

settings=my_flash_store.read();

stats=stats_store.read();
if (stats.magic != STATS_MAGIC) {     // first boot after upload: start fresh (written at the end of the first run)
  memset(&stats, 0, sizeof(stats));
  stats.magic = STATS_MAGIC;
  stats.heatRate = HEAT_RATE_DEFAULT;
  stats.coolRate = COOL_RATE_DEFAULT;
}

if (settings.value[11]==0)
{
settings.value[0] = 94; 
settings.value[1] = 60;   

settings.value[2] = 94;      
settings.value[3] = 120;  

settings.value[4] = 55;      
settings.value[5] = 80;  

settings.value[6] = 72;      
settings.value[7] = 40;  

settings.value[8] = 25;      
settings.value[9] = 180;  

settings.value[10] = 25;  


settings.value[11]=1;
my_flash_store.write(settings);

  };
  
}

void loop() {
  
  // read the analog in value:
 // analogReference(AR_EXTERNAL);

  setHeater(0, 0);
  sensorValue = analogRead(analogInPin);
  setHeater(temperature_mean, TEMPcontrol);

sensorVoltage=3.3*sensorValue/4096;
sensorResistance=((sensorVoltage*NTC_R0)/(3.3-sensorVoltage));
temperature=  1/(log(sensorResistance*1000/NTC_RN)/NTC_B+1/NTC_TN)-273.15 ;
if (sensorOK()) {
  if (isnan(temperature_mean)) temperature_mean=temperature;   // recover if the average ever went NaN
  temperature_mean= (temperature_mean*3+temperature)/4;
}

if (caseUX!=CASE_Error) checkSafety();

  // print the results to the Serial Monitor:
  //Serial.print("sensor = ");
  //  Serial.print(sensorValue);
 // Serial.print("\t output = ");
 // Serial.println(temperature);




switch (caseUX) {

case CASE_Main:

  
   if (counter>1) counter=0;
   if (counter<0) counter=1;
   MenuItem = counter;
   draw_main_display();
   
   if (!digitalRead(butPin)) {waitRelease();

   if (MenuItem==0) {caseUX=CASE_Run;casePCR=PCR_set; PCRstate=0; counter=0; coolInit=false;
     // reset run state so back-to-back runs start clean
     PCRcycle=1; TEMPi=0; TEMPd=0; PIDIntegration=false; noHeatTiming=false; runStart=millis();
     runCompleted=false; estAtStart=0; stats.runsStarted++;
     runHeatN=0; runCoolN=0; runHeatSecs=0; runCoolSecs=0; runHeatDeg=0; runCoolDeg=0;
     for (int i=0;i<10;i++) TEMPdif_a[i]=0;}
   if (MenuItem==1) {caseUX=CASE_Settings; counter=0;}

   }
   
    break;
    
case CASE_Settings:

    if (counter>12) counter=0;
    if (counter<0) counter=12;
    MenuItem = counter;
    if ((MenuItem%2==1)&&(settings.value[MenuItem]>90)) minuteMode=true; else minuteMode=false;
    if (!digitalRead(butPin)) 
    {waitRelease(); caseUX=CASE_EditSettings;
    if (MenuItem<11) {if (minuteMode) counter=settings.value[MenuItem]/60; else counter=settings.value[MenuItem];}}  // 11/12 = Set/Cancel, no value
    draw_setup_display();
    
    break;

case CASE_EditSettings:

    
    if (MenuItem%2==0)
     {if (MenuItem==10) 
         {if (counter<1) counter=1;
          if (counter>99) counter=99;}
     else 
        {if (counter<25) counter=25;
        if (counter>99) counter=99;}
     
     } // Temp Menu
     
     else
      {if (minuteMode)
       {if (counter<2) {minuteMode=false; counter=90;}
       }  //Minut Mode
       else
       {
       if (counter>90) {minuteMode=true; counter=2;}
       if (counter<0) {counter=0;}
       } //not Minut Mode
      } // Time Menu
    
    if (MenuItem==11) {caseUX=CASE_Main;   my_flash_store.write(settings);}
    if (MenuItem==12) {caseUX=CASE_Main;  settings=my_flash_store.read();}

    if (MenuItem<11) {if (minuteMode) settings.value[MenuItem]=counter*60; else settings.value[MenuItem]=counter;}
    if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Settings;counter=MenuItem;}
    draw_setup_display();
    
    break;

case CASE_Run:

  TEMPdif=TEMPset-temperature_mean;
  TEMPi=TEMPi+(TEMPset-temperature_mean);
  
  if (millis()-TEMPclick>200) {
  TEMPclick=millis();
  TEMPd=TEMPdif_a[4]-TEMPdif;
  TEMPdif_a[4]=TEMPdif_a[3];
  TEMPdif_a[3]=TEMPdif_a[2];
  TEMPdif_a[2]=TEMPdif_a[1];
  TEMPdif_a[1]=TEMPdif_a[0];
  TEMPdif_a[0]=TEMPdif;
  //  Serial.println (TEMPd);
  }
  
    switch (casePCR) {

    case PCR_set:
    TEMPset=settings.value[PCRstate*2];
    TIMEcontrol=settings.value[1+PCRstate*2];
    PIDIntegration=false;
    rampStartT=temperature_mean; rampStartMs=millis();
    if ((PCRstate==0) && (estAtStart==0)) estAtStart=estimateSecondsLeft();   // casePCR is still PCR_set here
    casePCR=PCR_transition;
    break;


    case PCR_transition:
    runPID();
    draw_run_display();
    if (abs(TEMPset-temperature_mean)<temperature_tollerance) {
      learnRampRate();
      PIDIntegration=true;TEMPi=0;TIMEclick=millis(); casePCR=PCR_time;}
    break;


    case PCR_time:
    runPID();
    TIMEcontrol=settings.value[1+PCRstate*2]-(millis()-TIMEclick)/1000;
    draw_run_display();

    if (TIMEcontrol<=0) {
      if (PCRstate==4) {
        caseUX=CASE_Done;
        runCompleted=true;
        stats.runsCompleted++;
        stats.lastEstSecs=estAtStart;
        stats.lastActualSecs=(millis()-runStart)/1000;
      }
    if (PCRstate==3)
    {PCRstate=1;
    PCRcycle++;
    if (PCRcycle>settings.value[10]) PCRstate=4;
    } else
    {PCRstate++;};
    casePCR=PCR_set;
    }
    break;

    
    default:
    // Statement(s)
    break;

    
    }//PCR switch

   break;

case CASE_Done:


  TEMPcontrol=0;
  setHeater(temperature_mean, TEMPcontrol);  // heater off

  // Auto cool-down: entered once per run (whether finished or stopped)
  if (!coolInit) {
    coolInit = true;
    coolActive = true;
    coolStart = millis();
    coolSampleTime = millis();
    coolRef = temperature_mean;
    runTotalSecs = (millis()-runStart)/1000;
    stats_store.write(stats);    // one flash write per run (finished or stopped)
    printStats();
  }
  if (coolActive) {
    if (millis()-coolSampleTime >= COOL_SAMPLE_MS) {
      if (coolRef-temperature_mean < COOL_STABLE_DROP) coolActive = false;  // no longer falling -> near ambient
      coolRef = temperature_mean;
      coolSampleTime = millis();
    }
    if (millis()-coolStart >= COOL_TIMEOUT_MS) coolActive = false;
  }
  if (coolActive) analogWrite(fanPin,255);  // fan off at loop start (setHeater) while ADC reads, then on again here

    draw_done_display();


if (!digitalRead(butPin)) {waitRelease(); coolActive=false; caseUX=CASE_Main;counter=MenuItem;}

break;

case CASE_Error:

  // Fault: heater off, fan full, stay here until the button is pressed
  TEMPcontrol=0;
  setHeater(0,0);
  analogWrite(fanPin,255);

    display.clearDisplay();
    display.setFont(&FreeSans9pt7b);
    display.fillRect(0,4,128,30,1);
    display.setTextColor(0);
    display.setCursor(4,25);
    display.println(errorReason);
    display.setFont();
    display.setTextColor(1);
    display.setCursor(0,40);
    display.print("Block: ");
    if (sensorOK()) {display.print(temperature_mean,1); display.print(" \xA7"); display.print("C");}
    else display.print("no reading");
    display.setCursor(0,54);
    display.print("Heater off. Press btn");
    display.display();

if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Main; counter=0;}

break;
      
default:
    // Statement(s)
    break;
    
} //switch
}




void draw_value_box(int x, int y, int val, bool highlight,bool edit)
{ int shift;

if ((val<46)&&(x>22)&&(x<100)) shift=-13; else shift=0;

  display.drawRect(x-3,y+2-val/2,17,val/2-1,1);
  
if (highlight&&!edit){
  display.fillRect(x-3,y+2-val/2+shift,17,11,1);
  display.setTextColor(0);
}
if (highlight&&edit) display.drawRect(x-2,y+2-val/2+shift,15,11,1);

 display.setCursor(x,y-val/2+4+shift);
 display.println(val);
 display.setTextColor(1);
};

void draw_value(int x, int y, int val, bool highlight,bool edit)
{
if (highlight&&!edit){
  display.fillRect(x-3,y-1,17,12,1);
  display.setTextColor(0);
    display.setCursor(x,y-1);

  } else   display.setCursor(x,y);

 //display.drawRect(x-3,y,2,7,1);
 //display.drawRect(x+12,y,2,7,1);
if (highlight&&edit){
 display.drawLine(x-3,y,x-3,y+6,1);
 display.drawLine(x+13,y,x+13,y+6,1);
}

 // display.drawRect(x-3,y-2,17,11,1);
  
  if (val>90) 
   { display.print(val/60); 
   display.println("m");} else
   {
    display.println(val); 
   }
   display.setTextColor(1);
}

void draw_setup_display()

{
  
display.clearDisplay(); 

 if (MenuItem<11) {
  
display.setCursor(5,0);
//display.print("Set ");

if (MenuItem<10){
display.print(SETTING_String[MenuItem/2]);
if (MenuItem%2==0) {display.print(" Temp. [");display.print("\xA7");display.print("C]");} else {display.print(" Time "); if (minuteMode) display.print("[min]"); else display.print("[s]");  }
} else display.print("No of cycles");
 } else 

 {
 if (MenuItem==12)   // Set or Cancel
 {

 display.setCursor(34,0);
 display.println("Set");
 display.fillRect(70,0,42,9,1);
 display.setTextColor(0);
 display.setCursor(75,1);
 display.println("Cancel");
 display.setTextColor(1);
 } else

 {
  display.fillRect(30,0,25,9,1);
 display.setTextColor(0);
 display.setCursor(34,1);
 display.println("Set");
 display.setTextColor(1);
 display.setCursor(75,0);
 display.println("Cancel");
 
  }
}


draw_value_box (21,55,settings.value[0],MenuItem==0,caseUX==CASE_EditSettings);
draw_value_box (43,55,settings.value[2],MenuItem==2,caseUX==CASE_EditSettings);
draw_value_box (63,55,settings.value[4],MenuItem==4,caseUX==CASE_EditSettings);
draw_value_box (83,55,settings.value[6],MenuItem==6,caseUX==CASE_EditSettings);
draw_value_box (105,55,settings.value[8],MenuItem==8,caseUX==CASE_EditSettings);


display.drawRect(38,45,61,11,1);
display.fillRect(38+1,45+1,61-2,11-2,0);


if ((MenuItem==10)&&!(caseUX==CASE_EditSettings)){
  display.fillRect(56,45,25,11,1);
  display.setTextColor(0);
}
if ((MenuItem==10)&&(caseUX==CASE_EditSettings))  display.drawRect(56,45,25,11,1);

 display.setCursor(60,47);
 display.print(settings.value[10]);
 display.print("x");
 display.setTextColor(1);

 

draw_value (21,57,settings.value[1],MenuItem==1,caseUX==CASE_EditSettings);
draw_value (43,57,settings.value[3],MenuItem==3,caseUX==CASE_EditSettings);
draw_value (63,57,settings.value[5],MenuItem==5,caseUX==CASE_EditSettings);
draw_value (83,57,settings.value[7],MenuItem==7,caseUX==CASE_EditSettings);
draw_value (105,57,settings.value[9],MenuItem==9,caseUX==CASE_EditSettings);

   

  display.display();
  
 // Serial.println (encoder0Pos, DEC);

    // Serial.println(Enc_counter,DEC);
   // Serial.print(millis());
   //    Serial.print(" :");
  //   Serial.println(temperature,DEC);
  
  
  }

void draw_main_display()
{
display.clearDisplay(); 

   display.setFont(&FreeSans9pt7b);


   if (MenuItem==0)   // Set or Cancel
 {

  display.fillRect(21,10,86,25,1);
 display.setTextColor(0);
 display.setCursor(26,28);
 display.println("Run PCR");

 //display.fillRect(21,38,86,25,1);
 display.setTextColor(1);
 display.setCursor(40,55);
 display.println("Setup");
 } else

 {
 // display.fillRect(21,10,86,25,1);
 display.setTextColor(1);
 display.setCursor(26,28);
 display.println("Run PCR");

 display.fillRect(21,38,86,25,1);
 display.setTextColor(0);
 display.setCursor(40,55);
 display.println("Setup");
 
  }
  display.setFont();

   display.setTextColor(1);

    display.display();

  }


void draw_run_display()
{
  char buf[24];
  int total = settings.value[10];
  // cycle shown in the title: 0 during Init, 1..N while cycling, N during Final
  int shownCycle = (PCRstate==0) ? 0 : ((PCRcycle>total) ? total : PCRcycle);

  display.clearDisplay();
  display.setFont();

  // Title bar (inverted): "PCR - 1 of 35", or the stop prompt after turning the knob
  display.fillRect(0,0,128,11,1);
  display.setTextColor(0);
  if (counter%2==0) {
    snprintf(buf, sizeof(buf), "PCR - %d of %d", shownCycle, total);
    drawCentered(2, buf);
  } else {
    drawCentered(2, "Press to STOP PCR");
    if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Done; counter=MenuItem;}
  }
  display.setTextColor(1);

  display.setCursor(0,14);
  display.print("State: ");
  display.print(SETTING_String[PCRstate]);

  // Temp: set / block
  display.setCursor(0,23);
  display.print("Temp: ");
  display.print(TEMPset,1);
  display.print("\xA7""C / ");
  display.print(temperature_mean,1);
  display.print("\xA7""C");

  display.drawLine(0,34,128,34,1);

  // Remaining time in the current step
  display.setCursor(0,37);
  display.print("Cycle Time: ");
  display.print(TIMEcontrol);
  display.print(" s");

  // Total time since the run started
  formatHMS(buf, sizeof(buf), (millis()-runStart)/1000);
  display.setCursor(0,46);
  display.print("Elapsed: ");
  display.print(buf);

  // Estimated time until the run finishes
  formatHMS(buf, sizeof(buf), estimateSecondsLeft());
  display.setCursor(0,55);
  display.print("Remaining: ");
  display.print(buf);

  display.display();
}

// rotate is called anytime the rotary inputs change state.
void rotate() {
  delayMicroseconds(500) ;

  unsigned char result = rotary.process();
  if (result == DIR_CW) {
    counter++;
   // Serial.println(counter);
  } else if (result == DIR_CCW) {
    counter--;
   // Serial.println(counter);
  }
}

int power_heating(float temperature,float power) 
{
float power_return;

#define FA_c 0.00564542
#define FA_d 0.0418254
#define FA_e 0.000019826
#define FA_f 0.003711

power_return = (FA_d-FA_f*temperature-power)/(FA_e*temperature-FA_c);

if (power_return>255) power_return=255;
if (power_return<0) power_return=0;

return (int)power_return;

}
  
int power_cooling(float temperature,float power) 
{
float power_return;
  
#define FA_i 0.000072781
#define FA_j 0.00413579
#define FA_k 0.001372876
#define FA_l 0.1204656

power_return =(FA_l-FA_j*temperature-power)/(FA_i*temperature-FA_k);

if (power_return>255) power_return=255;
if (power_return<0) power_return=0;

return (int)power_return;
}

void runPID()
{
   TEMPcontrol= PIDp*TEMPdif+PIDd*TEMPd+(int)PIDIntegration*PIDi*TEMPi;
  setHeater(temperature_mean, TEMPcontrol);

  }

  void setHeater(float temperature,float power)
{
  if (power==0) {
    pinMode(fanPin, OUTPUT);
    pinMode(heaterPin, OUTPUT);
    digitalWrite(fanPin,LOW);
    digitalWrite(heaterPin,LOW);
  }
  else 
  {
  if (TEMPcontrol>0)
  {
    heatPower=power_heating(temperature,power);
  analogWrite(fanPin,0);
  analogWrite(heaterPin,heatPower);
  }
  else 
  {
    heatPower=power_cooling(temperature,power);
  analogWrite(fanPin,heatPower);
  analogWrite(heaterPin, 0);
    } 
  } // else
}

// Turn the heater off, then wait for the button to be released.
// (Blocking waits used to leave the heater at its last PWM with no control.)
void waitRelease()
{
  setHeater(0,0);
  while (!digitalRead(butPin));
}

// Raw thermistor reading within plausible range (not shorted / open)
bool sensorOK()
{
  return (sensorValue > ADC_MIN) && (sensorValue < ADC_MAX) && !isnan(temperature);
}

void tripError(const char* reason)
{
  errorReason = reason;
  TEMPcontrol = 0;
  setHeater(0,0);
  noHeatTiming = false;
  caseUX = CASE_Error;
  Serial.print("SAFETY: "); Serial.println(reason);
}

// Called every loop (except in CASE_Error). Any fault -> heater off, fan on, error screen.
void checkSafety()
{
  if (!sensorOK()) { tripError("SENSOR ERR"); return; }
  if (temperature_mean > T_MAX) { tripError("OVER TEMP"); return; }

  // Heater at (near) full power but block not warming -> heater or sensor problem
  if ((caseUX==CASE_Run) && (TEMPcontrol>0) && (heatPower>=250) && (TEMPset-temperature_mean > NOHEAT_BELOW)) {
    if (!noHeatTiming) {
      noHeatTiming = true; noHeatStart = millis(); noHeatTemp = temperature_mean;
    } else if (millis()-noHeatStart >= NOHEAT_MS) {
      if (temperature_mean-noHeatTemp < NOHEAT_RISE) { tripError("NO HEATING"); return; }
      noHeatStart = millis(); noHeatTemp = temperature_mean;   // warming fine, start a new window
    }
  } else noHeatTiming = false;
}

// Print text horizontally centred at row y using the current font
void drawCentered(int y, const char* text)
{
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
  display.setCursor((SCREEN_WIDTH - bw) / 2, y);
  display.print(text);
}

// h:mm:ss
void formatHMS(char* buf, int len, unsigned long secs)
{
  snprintf(buf, len, "%lu:%02lu:%02lu", secs/3600, (secs/60)%60, secs%60);
}

// Seconds to ramp from one temperature to another into step st:
// use that step's measured mean rate if we have one in the same direction, else the overall rate
float rampSeconds(float from, float to, int st)
{
  float d = to - from;
  if (fabs(d) < 0.01) return 0;
  RampStep &rs = stats.step[st];
  float rate;
  if ((rs.n > 0) && ((rs.lastDelta > 0) == (d > 0)) && (rs.meanRate > 0)) rate = rs.meanRate;
  else rate = (d > 0) ? stats.heatRate : stats.coolRate;
  return fabs(d)/rate;
}

// Called when a ramp reaches its target. Records it for the step and blends it into the overall rate.
void learnRampRate()
{
  float d = TEMPset - rampStartT;
  float secs = (millis()-rampStartMs)/1000.0;
  if (fabs(d) < RAMP_MIN_DELTA || secs < 1) return;
  float r = fabs(d)/secs;
  if (r < 0.05) r = 0.05;
  if (r > 10) r = 10;

  if (d > 0) { stats.heatRate = (stats.heatRate + r)/2; stats.rampsHeat++; runHeatN++; runHeatSecs += secs; runHeatDeg += d; }
  else       { stats.coolRate = (stats.coolRate + r)/2; stats.rampsCool++; runCoolN++; runCoolSecs += secs; runCoolDeg -= d; }

  RampStep &rs = stats.step[PCRstate];
  rs.n++;
  rs.meanRate += (r - rs.meanRate) / rs.n;
  if ((rs.n == 1) || (r < rs.minRate)) rs.minRate = r;
  if ((rs.n == 1) || (r > rs.maxRate)) rs.maxRate = r;
  rs.lastSecs = secs;
  rs.lastDelta = d;
}

// Dump the stats to USB serial (115200) at the end of each run, for tuning
void printStats()
{
  Serial.println("--- PocketPCR ramp stats ---");
  Serial.print("runs started/completed: "); Serial.print(stats.runsStarted); Serial.print("/"); Serial.println(stats.runsCompleted);
  Serial.print("last run est/actual s: "); Serial.print(stats.lastEstSecs); Serial.print("/"); Serial.println(stats.lastActualSecs);
  Serial.print("overall heat/cool C/s: "); Serial.print(stats.heatRate,3); Serial.print("/"); Serial.print(stats.coolRate,3);
  Serial.print("  ramps: "); Serial.print(stats.rampsHeat); Serial.print("/"); Serial.println(stats.rampsCool);
  for (int i=0;i<5;i++) {
    RampStep &rs = stats.step[i];
    Serial.print(SETTING_String[i]); Serial.print(": n="); Serial.print(rs.n);
    Serial.print(" mean="); Serial.print(rs.meanRate,3); Serial.print(" min="); Serial.print(rs.minRate,3);
    Serial.print(" max="); Serial.print(rs.maxRate,3); Serial.print(" C/s  last "); Serial.print(rs.lastDelta,1);
    Serial.print("C in "); Serial.print(rs.lastSecs,1); Serial.println("s");
  }
}

// Estimate of the time left in the run: the rest of the current step, then every remaining step
// (ramp at the learned rate + hold time), following the same order as the PCR state machine.
unsigned long estimateSecondsLeft()
{
  float secs;
  if (casePCR == PCR_time) secs = (TIMEcontrol > 0) ? TIMEcontrol : 0;
  else secs = rampSeconds(temperature_mean, TEMPset, PCRstate) + settings.value[1+PCRstate*2];

  int st = PCRstate, cyc = PCRcycle, total = settings.value[10];
  float prevT = TEMPset;
  while (st != 4) {
    if (st == 3) { st = 1; cyc++; if (cyc > total) st = 4; }
    else st++;
    float t = settings.value[st*2];
    secs += rampSeconds(prevT, t, st) + settings.value[1+st*2];
    prevT = t;
  }
  return (unsigned long)secs;
}

// One ramp summary line, e.g. "Heat: 1.82°C/s ~12s" (time-weighted rate, average seconds per ramp)
void printRampLine(const char* label, uint32_t n, float secs, float deg)
{
  display.print(label);
  if (n == 0 || secs <= 0) { display.print("--"); return; }
  display.print(deg/secs, 2);
  display.print("\xA7""C/s ~");
  display.print((int)(secs/n + 0.5));
  display.print("s");
}

// Done screen: run summary + cool-down status
void draw_done_display()
{
  char buf[24];
  display.clearDisplay();
  display.setFont();

  display.fillRect(0,0,128,11,1);
  display.setTextColor(0);
  drawCentered(2, runCompleted ? "PCR Done" : "PCR Stopped");
  display.setTextColor(1);

  formatHMS(buf, sizeof(buf), runTotalSecs);
  display.setCursor(0,14);
  display.print("Total Time: "); display.print(buf);

  display.setCursor(0,23);
  display.print("Estimated:  ");
  if (estAtStart > 0) { formatHMS(buf, sizeof(buf), estAtStart); display.print(buf); } else display.print("--");

  display.setCursor(0,32);
  printRampLine("Heat: ", runHeatN, runHeatSecs, runHeatDeg);
  display.setCursor(0,41);
  printRampLine("Cool: ", runCoolN, runCoolSecs, runCoolDeg);

  display.drawLine(0,51,128,51,1);
  display.setCursor(0,55);
  display.print(coolActive ? "Cooling: " : "Block: ");
  display.print(temperature_mean,1);
  display.print("\xA7""C");

  display.display();
}

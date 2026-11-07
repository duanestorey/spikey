/*
PocketPCR PCR Thermocycler by GaudiLabs
Pocket size USB powered PCR Thermo Cycler

http://gaudi.ch/PocketPCR/

https://github.com/GaudiLabs/PocketPCR

Modified as "Spikey" by Duane Storey, 2026 (https://github.com/duanestorey/spikey):
touchdown PCR, RT program, Room temperature steps, safety cutoffs, time estimate,
run/Done screens, auto cool-down and more. See README.md for the full changelog.
Modifications are licensed under the GNU GPL v3.0, the same as the original.

*/

#include <math.h> 
#include "Adafruit_GFX.h"
#include <Adafruit_SSD1306.h>
#include "Rotary.h"
#include "FlashStorage.h"

#include <Fonts/FreeSans9pt7b.h>

const char VersionString[] = "Spikey 1.0.0";   // fork of GaudiLabs PocketPCR V1.02 2020
#define SPLASH_MS 5000                         // how long the start-up splash is shown
#define ENABLE_SQPCR 0        // 1 = include the sqPCR program (reading pauses). Held back until 1.1.0

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
#define CASE_Config 7
#define CASE_Debug 8
#define CASE_Paused 9
#define CASE_About 10

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
#define HEAT_RATE_DEFAULT 1.06   // measured on this device, 2026-11-07: 35-cycle run, 5 tubes (hardware-specific)
#define COOL_RATE_DEFAULT 0.95   // measured on this device, 2026-11-07: 35-cycle run, 5 tubes, stock fan
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
int tdStart;          // touchdown: anneal START temp (value[4] is the anneal END temp)
int pauseFirst;       // sqPCR only: first pause (baseline) after Extension of this cycle
int pauseSecond;      // sqPCR only: second pause cycle (<= first = none)
int pauseEvery;       // sqPCR only: then every N cycles after the second (adjustable on the pause screen)
} EEprom;

// Touchdown PCR: anneal starts at tdStart and drops TD_STEP °C per cycle until it reaches value[4],
// then stays there. value[10] is the TOTAL cycle count (touchdown included). Off when tdStart <= value[4]
// (or end is Room). The editor keeps value[10] >= minCycles() so the touchdown always completes.
#define TD_STEP 1
#define TD_START_DEFAULT 63
int annealPhase = 0;      // editing the Annealing temp: 0 = start, 1 = end
bool tdLinked = false;    // touchdown was off when editing began: end follows start

FlashStorage(my_flash_store, EEprom);
FlashStorage(sqpcr_store, EEprom);      // sqPCR program: same layout as PCR + pause fields

// Measured ramp statistics, kept in flash across power cycles (reset by a firmware upload).
// Saved once per run (when it ends) to spare the flash. Used for the time-left estimate and for tuning.
#define STATS_MAGIC 0x52414D32   // "RAM2": bump if the layout changes
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
  RampStep step[8];              // by target step: 0-4 PCR (Init, Denature, Annealing, Extension, Final), 5-7 RT (Incubation, Transcribe, Deactivate)
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
EEprom settings;    // PCR program
EEprom sqsettings;   // sqPCR program

// Program types. PCR uses `settings` (GaudiLabs layout), sqPCR `sqsettings`, RT its own store.
#define RUN_PCR 0
#define RUN_RT 1
#define RUN_SQPCR 2
#define N_TYPES (ENABLE_SQPCR ? 3 : 2)   // program types in the menus

// sqPCR manual-reading pauses: after Extension of nextPauseCycle the run holds at the Extension temp
#define PAUSE_MAX_N 9       // pause numbers (first, second, every, Resume [N]) are single digits
int nextPauseCycle = 0;     // 0 = no more pauses
int pauseN = 5;             // cycles to the next pause (Resume [N])
int userN = 0;              // N the user set on the pause screen this run (0 = follow the schedule)
int pauseNshown = 0;        // N when editing began (to detect a change)
int pauseSel = 0;           // pause screen selection
bool pauseEditing = false;  // editing N on the pause screen
unsigned long pauseStartMs = 0, pausedMs = 0;
int pauseCount = 0;
int runType = RUN_PCR;     // program being run
int editType = RUN_PCR;    // program being edited in Config

// "Room" temperature: lowest selectable value (shown "RT"). Heater off, step timer starts at once.
#define TEMP_ROOM 0
#define TEMP_ROOM_COUNTER 24     // editor value just below the 25 °C minimum that means Room
#define ROOM_FAN_ABOVE 30.0      // during a Room step, run the fan while the block is warmer than this
#define ROOM_TEMP_EST 25.0       // assumed block temperature after a Room step (for the estimate)
bool roomStep = false;

// RT program: Incubation, Transcribe, Deactivate (temp/time pairs), one pass, then auto cool-down
#define RT_MAGIC 0x52545031      // "RTP1"
typedef struct {
  uint32_t magic;
  int value[6];
} RTprog;
FlashStorage(rt_store, RTprog);
RTprog rtprog;
  

int sensorValue = 0;        // value read from the sesnor
int outputValue = 0;        // value output to the PWM (heater)
float temperature =0;
float temperature_mean =0;

float sensorResistance =0;
float sensorVoltage =0;

boolean editMode = false;
boolean minuteMode = false;

volatile int counter = 0;   // changed by the encoder interrupt (rotate())
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
  draw_splash();             // also shown by Config > About
  display.dim(false);
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

rtprog=rt_store.read();
if (rtprog.magic != RT_MAGIC) {      // first boot after upload: RT defaults
  rtprog.magic = RT_MAGIC;
  rtprog.value[0] = TEMP_ROOM; rtprog.value[1] = 600;   // Incubation: room temp, 10 min
  rtprog.value[2] = 55;        rtprog.value[3] = 1200;  // Transcribe: 55 °C, 20 min
  rtprog.value[4] = 85;        rtprog.value[5] = 300;   // Deactivate: 85 °C, 5 min
  rt_store.write(rtprog);
}

if (settings.value[11]==0)            // first boot after upload: default PCR program
{
settings.value[0] = 95;  settings.value[1] = 300;   // Init: 95 °C, 5 min
settings.value[2] = 94;  settings.value[3] = 30;    // Denature: 94 °C, 30 s
settings.value[4] = 55;  settings.value[5] = 30;    // Annealing: touchdown 63 -> 55 °C, 30 s
settings.tdStart = TD_START_DEFAULT;                //   (TD_START_DEFAULT = 63)
settings.value[6] = 72;  settings.value[7] = 30;    // Extension: 72 °C, 30 s
settings.value[8] = 72;  settings.value[9] = 300;   // Final extension: 72 °C, 5 min
settings.value[10] = 35;                            // total cycles (touchdown included)
settings.pauseFirst = 1; settings.pauseSecond = 6; settings.pauseEvery = 3;  // unused for PCR

settings.value[11]=1;
my_flash_store.write(settings);

  };

sqsettings=sqpcr_store.read();
if (sqsettings.value[11]==0)           // first boot after upload: default sqPCR program (PCR + pauses)
{
sqsettings = settings;                 // same steps / touchdown as the PCR defaults...
sqsettings.value[10] = 40;             // ...40 cycles total
sqsettings.pauseFirst = 1;             // baseline reading after the first Extension (same temp/chemistry as later reads)
sqsettings.pauseSecond = 6;            // second reading after cycle 6
sqsettings.pauseEvery = 3;             // then every 3 cycles (transitions span ~6-7 cycles, so 2-3 points in it)
sqsettings.value[11]=1;
sqpcr_store.write(sqsettings);
}
  
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

   if (counter>N_TYPES) counter=0;
   if (counter<0) counter=N_TYPES;
   MenuItem = counter;
   draw_main_display();

   if (!digitalRead(butPin)) {waitRelease();
     if (MenuItem<N_TYPES) startRun(menuType(MenuItem));      // Run PCR / (Run sqPCR) / Run RT
     else {caseUX=CASE_Config; counter=0;}
   }

    break;

case CASE_Config:
  {
    if (counter>N_TYPES+2) counter=0;
    if (counter<0) counter=N_TYPES+2;
    int item = counter;
    draw_config_display(item);
    if (!digitalRead(butPin)) {waitRelease();
      if (item<N_TYPES) {editType = menuType(item); caseUX=CASE_Settings; counter=0;}   // PCR / (sqPCR) / RT program
      else if (item==N_TYPES) {caseUX=CASE_Debug; counter=0;}
      else if (item==N_TYPES+1) {caseUX=CASE_About;}
      else {caseUX=CASE_Main; counter=N_TYPES;}
    }
  }
    break;

case CASE_About:                 // the start-up splash again; press to go back
    draw_splash();
    if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Config; counter=N_TYPES+1;}
    break;

case CASE_Debug:

    if (counter>2) counter=0;
    if (counter<0) counter=2;
    draw_debug_display(counter);
    if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Config; counter=N_TYPES;}

    break;

case CASE_Settings:
  {
    int* ev = progVals(editType);
    int last = lastValItem(editType);
    if (counter>last+2) counter=0;
    if (counter<0) counter=last+2;
    MenuItem = counter;
    if ((MenuItem<=last)&&(MenuItem%2==1)&&!(pcrLike(editType)&&(MenuItem>=10))&&(ev[MenuItem]>90)) minuteMode=true; else minuteMode=false;
    if (!digitalRead(butPin))
    {waitRelease(); caseUX=CASE_EditSettings;
     if (MenuItem<=last) {                       // last+1 / last+2 = Set / Cancel, no value
       if (isAnnealItem()) {                     // touchdown-aware: edit the start first
         EEprom &P = prog(editType);
         annealPhase=0; tdLinked=!tdOn(editType) && (P.value[4]!=TEMP_ROOM);   // a Room end stays Room
         counter = tdLinked ? P.value[4] : P.tdStart;
       }
       else if ((editType==RUN_SQPCR)&&(MenuItem==11)) counter=sqsettings.pauseFirst;
       else if ((editType==RUN_SQPCR)&&(MenuItem==12)) counter=sqsettings.pauseSecond;
       else if ((editType==RUN_SQPCR)&&(MenuItem==13)) counter=sqsettings.pauseEvery;
       else if (isTempItem(editType,MenuItem)) counter = (ev[MenuItem]==TEMP_ROOM) ? TEMP_ROOM_COUNTER : ev[MenuItem];
       else if (minuteMode) counter=ev[MenuItem]/60; else counter=ev[MenuItem];
     }
    }
    draw_setup_display();
  }
    break;

case CASE_EditSettings:
  {
    int* ev = progVals(editType);
    int last = lastValItem(editType);

    if (MenuItem==last+1) {                      // Set: save and back to Config
      if (editType==RUN_RT) rt_store.write(rtprog);
      else if (editType==RUN_SQPCR) sqpcr_store.write(sqsettings);
      else my_flash_store.write(settings);
      caseUX=CASE_Config; counter=typeMenuIndex(editType); break;
    }
    if (MenuItem==last+2) {                      // Cancel: reload and back to Config
      if (editType==RUN_RT) rtprog=rt_store.read();
      else if (editType==RUN_SQPCR) sqsettings=sqpcr_store.read();
      else settings=my_flash_store.read();
      caseUX=CASE_Config; counter=typeMenuIndex(editType); break;
    }

    if (isAnnealItem()) {
      EEprom &P = prog(editType);
      if (annealPhase==0) {                                          // start: 25..99
        if (counter<25) counter=25;
        if (counter>99) counter=99;
        P.tdStart=counter;
        if (tdLinked) P.value[4]=counter;                            // touchdown off: end follows start
        else if ((P.value[4]!=TEMP_ROOM)&&(P.value[4]>counter)) P.value[4]=counter;
        if (P.value[10]<minCycles(editType)) P.value[10]=minCycles(editType);   // touchdown needs more cycles
        if (!digitalRead(butPin)) {waitRelease(); annealPhase=1;
          counter=(P.value[4]==TEMP_ROOM) ? TEMP_ROOM_COUNTER : P.value[4];}
      } else {                                                       // end: Room..start
        if (counter<TEMP_ROOM_COUNTER) counter=TEMP_ROOM_COUNTER;
        if (counter>P.tdStart) counter=P.tdStart;
        P.value[4] = (counter==TEMP_ROOM_COUNTER) ? TEMP_ROOM : counter;
        if (P.value[10]<minCycles(editType)) P.value[10]=minCycles(editType);   // touchdown needs more cycles
        if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Settings; counter=MenuItem;}
      }
      draw_setup_display();
      break;
    }

    if (isTempItem(editType,MenuItem))
       {if (counter<TEMP_ROOM_COUNTER) counter=TEMP_ROOM_COUNTER;   // 24 = Room
        if (counter>99) counter=99;
        ev[MenuItem] = (counter==TEMP_ROOM_COUNTER) ? TEMP_ROOM : counter;}
    else if (pcrLike(editType)&&(MenuItem==10))                     // cycle count (total, touchdown included)
       {if (counter<minCycles(editType)) counter=minCycles(editType);  // never shorter than a full touchdown
        if (counter>99) counter=99;
        ev[MenuItem]=counter;
        if (editType==RUN_SQPCR) clampPauses();}
    else if ((editType==RUN_SQPCR)&&(MenuItem==11))                   // sqPCR: first (baseline) pause cycle
       {if (counter<1) counter=1;
        if (counter>PAUSE_MAX_N-1) counter=PAUSE_MAX_N-1;         // leave room for a later second pause
        if (counter>sqsettings.value[10]) counter=sqsettings.value[10];
        sqsettings.pauseFirst=counter;
        clampPauses();}
    else if ((editType==RUN_SQPCR)&&(MenuItem==12))                   // sqPCR: second pause cycle (after first)
       {if (counter<=sqsettings.pauseFirst) counter=sqsettings.pauseFirst+1;
        if (counter>PAUSE_MAX_N) counter=PAUSE_MAX_N;
        if (counter>sqsettings.value[10]) counter=sqsettings.value[10];
        sqsettings.pauseSecond=counter;}
    else if ((editType==RUN_SQPCR)&&(MenuItem==13))                   // sqPCR: then every N cycles
       {if (counter<1) counter=1;
        if (counter>PAUSE_MAX_N) counter=PAUSE_MAX_N;
        sqsettings.pauseEvery=counter;}
    else                                                             // time
      {if (minuteMode)
       {if (counter<2) {minuteMode=false; counter=90;}
        if (counter>99) counter=99;                               // "99m" is the widest that fits
       }  //Minute Mode
       else
       {
       if (counter>90) {minuteMode=true; counter=2;}
       if (counter<0) {counter=0;}
       } //not Minute Mode
       if (minuteMode) ev[MenuItem]=counter*60; else ev[MenuItem]=counter;
      }

    if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Settings;counter=MenuItem;}
    draw_setup_display();
  }
    break;

case CASE_Run:

  updatePIDTerms();
  
    switch (casePCR) {

    case PCR_set:
    roomStep = (stepTemp(PCRstate)==TEMP_ROOM);
    TEMPset = roomStep ? ROOM_TEMP_EST : stepTemp(PCRstate);
    TIMEcontrol=stepTime(PCRstate);
    PIDIntegration=false;
    rampStartT=temperature_mean; rampStartMs=millis();
    if ((PCRstate==0) && (estAtStart==0)) estAtStart=estimateSecondsLeft();   // casePCR is still PCR_set here
    if (roomStep) {TEMPcontrol=0; TIMEclick=millis(); casePCR=PCR_time;}    // Room: no ramp, timer starts now
    else casePCR=PCR_transition;
    break;


    case PCR_transition:
    runPID();
    draw_run_display();
    if (abs(TEMPset-temperature_mean)<temperature_tollerance) {
      learnRampRate();
      PIDIntegration=true;TEMPi=0;TIMEclick=millis(); casePCR=PCR_time;}
    break;


    case PCR_time:
    if (roomStep) {                               // Room: heater off, fan only while the block is warm
      TEMPcontrol=0;
      if (temperature_mean > ROOM_FAN_ABOVE) analogWrite(fanPin,255);
    } else runPID();
    TIMEcontrol=stepTime(PCRstate)-(millis()-TIMEclick)/1000;
    draw_run_display();

    if ((TIMEcontrol<=0)&&(caseUX==CASE_Run)) {
      if ((runType==RUN_SQPCR)&&(PCRstate==3)&&(nextPauseCycle>0)&&(PCRcycle==nextPauseCycle)) {
        enterPause();                               // sqPCR reading: hold at Extension temp
        break;
      }
      advanceStep();
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
    runTotalSecs = (millis()-runStart-pausedMs)/1000;   // excludes sqPCR reading pauses
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


if (!digitalRead(butPin)) {waitRelease(); coolActive=false; caseUX=CASE_Main; counter=typeMenuIndex(runType);}

break;

case CASE_Paused:

  // sqPCR reading pause: keep holding the Extension temperature
  updatePIDTerms();
  runPID();

  if (pauseEditing) {                       // editing Resume [N]: press resumes
    if (counter<1) counter=1;
    if (counter>PAUSE_MAX_N) counter=PAUSE_MAX_N;
    pauseN=counter;
    if (!digitalRead(butPin)) {waitRelease(); pauseEditing=false;
      if (counter!=pauseNshown) userN=pauseN;               // user changed it: keep it for this run
      resumeRun(PCRcycle+pauseN);}
  } else {
    if (counter>2) counter=0;
    if (counter<0) counter=2;
    pauseSel=counter;
    if (!digitalRead(butPin)) {waitRelease();
      if (pauseSel==0) {pauseEditing=true; counter=pauseN; pauseNshown=pauseN;}  // Resume [N]: press to edit, press again to go
      else if (pauseSel==1) resumeRun(0);                         // no more pauses
      else {pausedMs+=millis()-pauseStartMs; caseUX=CASE_Done; counter=0;   // Stop run
            Serial.println("STOP during pause");}
    }
  }
  if (caseUX==CASE_Paused) draw_pause_display();

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

if (!digitalRead(butPin)) {waitRelease();
  settings=my_flash_store.read(); sqsettings=sqpcr_store.read(); rtprog=rt_store.read();   // drop unsaved edits if the error hit mid-edit
  caseUX=CASE_Main; counter=0;}

break;
      
default:
    // Statement(s)
    break;
    
} //switch
}




void draw_value_box(int x, int y, int val, bool highlight,bool edit,bool noShift=false)
{ int shift;
  bool room = (val==TEMP_ROOM);
  if (room) val = TEMP_ROOM_COUNTER;          // draw Room at the height of 24 °C

if ((!noShift)&&(val<46)&&(x>22)&&(x<100)) shift=-13; else shift=0;   // PCR only: dodge the cycles box

  display.drawRect(x-3,y+2-val/2,17,val/2-1,1);

if (highlight&&!edit){
  display.fillRect(x-3,y+2-val/2+shift,17,11,1);
  display.setTextColor(0);
}
if (highlight&&edit) display.drawRect(x-2,y+2-val/2+shift,15,11,1);

 display.setCursor(x,y-val/2+4+shift);
 if (room) display.println("RT"); else display.println(val);
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
  int* ev = progVals(editType);
  int last = lastValItem(editType);
  bool editing = (caseUX==CASE_EditSettings);

  display.clearDisplay();
  display.setFont();

  // Header: what is selected, or the Set / Cancel choice
  if (MenuItem<=last) {
    // centred, max 21 chars (e.g. "Incubation Temp. [°C]")
    char hdr[24];
    const char* nm = stepName(editType, MenuItem/2);
    EEprom &P = prog(editType);
    if (pcrLike(editType)&&(MenuItem==10)) snprintf(hdr, sizeof(hdr), "No of cycles");
    else if ((editType==RUN_SQPCR)&&(MenuItem==11)) snprintf(hdr, sizeof(hdr), "First pause: cycle %d", sqsettings.pauseFirst);
    else if ((editType==RUN_SQPCR)&&(MenuItem==12)) snprintf(hdr, sizeof(hdr), "Second pause: cycle %d", sqsettings.pauseSecond);
    else if ((editType==RUN_SQPCR)&&(MenuItem==13)) snprintf(hdr, sizeof(hdr), "Then every %d cycle%s", sqsettings.pauseEvery, sqsettings.pauseEvery==1 ? "" : "s");
    else if (isAnnealItem() && editing && annealPhase==0) snprintf(hdr, sizeof(hdr), "Anneal start [\xA7""C]");
    else if (isAnnealItem() && editing) snprintf(hdr, sizeof(hdr), (P.value[4]==TEMP_ROOM) ? "Anneal end [RT]" : "Anneal end [\xA7""C]");
    else if (isAnnealItem() && tdOn(editType)) snprintf(hdr, sizeof(hdr), "Annealing %d>%d [\xA7""C]", P.tdStart, P.value[4]);
    else if (MenuItem%2==0) {
      if (ev[MenuItem]==TEMP_ROOM) snprintf(hdr, sizeof(hdr), "%s Temp. [RT]", nm);
      else snprintf(hdr, sizeof(hdr), "%s Temp. [\xA7""C]", nm);
    } else snprintf(hdr, sizeof(hdr), "%s Time %s", nm, minuteMode ? "[min]" : "[s]");
    drawCentered(0, hdr);
  } else if (MenuItem==last+2) {   // Cancel highlighted
    display.setCursor(34,0);
    display.println("Set");
    display.fillRect(70,0,42,9,1);
    display.setTextColor(0);
    display.setCursor(75,1);
    display.println("Cancel");
    display.setTextColor(1);
  } else {                         // Set highlighted
    display.fillRect(30,0,25,9,1);
    display.setTextColor(0);
    display.setCursor(34,1);
    display.println("Set");
    display.setTextColor(1);
    display.setCursor(75,0);
    display.println("Cancel");
  }

  if (pcrLike(editType)) {
    const int xs[5] = {21,43,63,83,105};
    for (int i=0;i<5;i++) {
      if ((i==2)&&tdOn(editType)) draw_anneal_box(xs[i],55,MenuItem==4,editing);
      else draw_value_box(xs[i],55,ev[i*2],MenuItem==i*2,editing);
    }

    // cycles box
    display.drawRect(38,45,61,11,1);
    display.fillRect(38+1,45+1,61-2,11-2,0);
    if (editType==RUN_SQPCR) {                 // sqPCR: "40x P1/6/3" = cycles, first, second, every
      char seg[4][8];
      snprintf(seg[0], 8, "%dx", ev[10]);
      snprintf(seg[1], 8, "P%d", sqsettings.pauseFirst);
      snprintf(seg[2], 8, "/%d", sqsettings.pauseSecond);
      snprintf(seg[3], 8, "/%d", sqsettings.pauseEvery);
      int tw = 3;                             // text width incl. the gap before "P"
      for (int i=0;i<4;i++) tw += strlen(seg[i])*6;
      int bw = (tw+6 > 61) ? tw+6 : 61;       // box grows to fit, centred on the original box
      int bx = 68 - bw/2;
      display.drawRect(bx,45,bw,11,1);
      display.fillRect(bx+1,46,bw-2,9,0);
      int x = 68 - tw/2;
      for (int i=0;i<4;i++) {
        if (i==1) x += 3;                     // small gap before the pause part
        int w = strlen(seg[i])*6;
        if ((MenuItem==10+i)&&!editing) {display.fillRect(x-1,46,w+1,9,1); display.setTextColor(0);}
        if ((MenuItem==10+i)&&editing) display.drawRect(x-1,46,w+1,9,1);
        display.setCursor(x,47);
        display.print(seg[i]);
        display.setTextColor(1);
        x += w;
      }
    } else {
      if ((MenuItem==10)&&!editing){
        display.fillRect(56,45,25,11,1);
        display.setTextColor(0);
      }
      if ((MenuItem==10)&&editing)  display.drawRect(56,45,25,11,1);
      display.setCursor(60,47);
      display.print(ev[10]);
      display.print("x");
      display.setTextColor(1);
    }

    for (int i=0;i<5;i++) draw_value(xs[i],57,ev[i*2+1],MenuItem==i*2+1,editing);
  } else {
    const int xs[3] = {30,62,94};
    for (int i=0;i<3;i++) draw_value_box(xs[i],55,ev[i*2],MenuItem==i*2,editing,true);
    for (int i=0;i<3;i++) draw_value(xs[i],57,ev[i*2+1],MenuItem==i*2+1,editing);
  }

  display.display();
}

// Plain centred title with a separator line under it (menus and Debug)
void draw_title(const char* title)
{
  display.setTextColor(1);
  drawCentered(1, title);
  display.drawLine(0,11,127,11,1);
}

// Menu list in the small font under a plain title + separator (main menu and Config)
void draw_menu(const char* title, const char* const* items, int n, int sel)
{
  display.clearDisplay();
  display.setFont();
  draw_title(title);
  int y0 = (n>4) ? 14 : 16, pitch = (n>4) ? 10 : 12;   // 5 rows need a tighter pitch
  const int rows = 5;                                  // at most 5 rows fit; longer lists scroll
  int first = (n>rows && sel>=rows) ? sel-rows+1 : 0;
  for (int i=first;i<n && i<first+rows;i++) {
    int y = y0 + (i-first)*pitch;
    if (sel==i) {display.fillRect(0,y-(n>4 ? 1 : 2),128,(n>4 ? 10 : 11),1); display.setTextColor(0);}
    else display.setTextColor(1);
    drawCentered(y, items[i]);
  }
  display.setTextColor(1);
  display.display();
}

// Start-up splash: 5 lines (line 3 blank), small built-in font, centred on screen
void draw_splash()
{
  static const char* const splash[] = { "PocketPCR", "by GaudiLabs", "", "Version", VersionString };
  const int n = 5, lineH = 10;
  int y = (SCREEN_HEIGHT - (n*lineH - 2)) / 2;   // -2: last line needs only its 8 px glyph height
  display.clearDisplay();
  display.setFont();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  for (int i=0; i<n; i++) { drawCentered(y + i*lineH, splash[i]); }
  display.display();
}

void draw_main_display()
{
#if ENABLE_SQPCR
  static const char* const items[] = {"Run PCR", "Run sqPCR", "Run RT", "Config >"};
#else
  static const char* const items[] = {"Run PCR", "Run RT", "Config >"};
#endif
  draw_menu("Main Menu", items, N_TYPES+1, MenuItem);
}

void draw_config_display(int item)
{
#if ENABLE_SQPCR
  static const char* const items[] = {"PCR Program", "sqPCR Program", "RT Program", "Debug", "About", "< Back"};
#else
  static const char* const items[] = {"PCR Program", "RT Program", "Debug", "About", "< Back"};
#endif
  draw_menu("Config", items, N_TYPES+3, item);
}

// Debug pages: saved ramp statistics (knob = page, button = back)
void draw_debug_display(int page)
{
  char buf[24];
  display.clearDisplay();
  display.setFont();
  const char* titles[3] = {"Debug 1/3", "Debug 2/3 PCR", "Debug 3/3 RT"};
  draw_title(titles[page]);

  if (page==0) {
    display.setCursor(0,14); display.print("Runs started: "); display.print(stats.runsStarted);
    display.setCursor(0,23); display.print("Runs done:    "); display.print(stats.runsCompleted);
    formatHMS(buf, sizeof(buf), stats.lastEstSecs);
    display.setCursor(0,32); display.print("Last est: "); display.print(buf);
    formatHMS(buf, sizeof(buf), stats.lastActualSecs);
    display.setCursor(0,41); display.print("Last act: "); display.print(buf);
    display.setCursor(0,50); display.print("Heat/Cool: ");
    display.print(stats.heatRate,2); display.print("/"); display.print(stats.coolRate,2);
  } else {
    int type = (page==1) ? RUN_PCR : RUN_RT;
    int base = (page==1) ? 0 : 5;
    for (int i=0;i<numSteps(type);i++) {
      RampStep &rs = stats.step[base+i];
      display.setCursor(0,14+i*9);
      display.print(stepName(type,i));
      display.setCursor(66,14+i*9);
      display.print("n"); display.print(rs.n);
      display.setCursor(96,14+i*9);
      if (rs.n>0) display.print(rs.meanRate,2); else display.print("--");
    }
  }
  display.display();
}


void draw_run_display()
{
  char buf[24];
  int total = totalCycles();
  // PCR title cycle: 0 during Init, 1..N while cycling, N during Final
  int shownCycle = (PCRstate==0) ? 0 : ((PCRcycle>total) ? total : PCRcycle);

  display.clearDisplay();
  display.setFont();

  // Title bar (inverted): "PCR - 1 of 35", or the stop prompt after turning the knob
  display.fillRect(0,0,128,11,1);
  display.setTextColor(0);
  if (counter%2==0) {
    if (runType==RUN_RT) snprintf(buf, sizeof(buf), "RT - Step %d of %d", PCRstate+1, numSteps(RUN_RT));
    else if ((PCRstate>0)&&(PCRstate<4)&&(PCRcycle<=tdCycles(runType))) snprintf(buf, sizeof(buf), "%s - %d of %d TD", typeName(runType), shownCycle, total);
    else snprintf(buf, sizeof(buf), "%s - %d of %d", typeName(runType), shownCycle, total);
    drawCentered(2, buf);
  } else {
    snprintf(buf, sizeof(buf), "Press to STOP %s", typeName(runType));
    drawCentered(2, buf);
    if (!digitalRead(butPin)) {waitRelease(); caseUX=CASE_Done; counter=MenuItem;}
  }
  display.setTextColor(1);

  display.setCursor(0,14);
  display.print("State: ");
  display.print(stepName(runType,PCRstate));

  // Temp: set / block
  display.setCursor(0,23);
  display.print("Temp: ");
  if (roomStep) display.print("Room / ");
  else {display.print(TEMPset,1); display.print("\xA7""C / ");}
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

  // sqPCR with pauses still to come: time to the next reading; otherwise time to the end
  display.setCursor(0,55);
  if ((runType==RUN_SQPCR)&&(nextPauseCycle>0)) {
    formatHMS(buf, sizeof(buf), estimateSecondsLeft(nextPauseCycle));
    display.print("Next Pause: ");
  } else {
    formatHMS(buf, sizeof(buf), estimateSecondsLeft());
    display.print("Remaining: ");
  }
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

// Seconds to ramp from `from` to a step's raw target temperature, for step st of the running program:
// that step's measured mean rate if we have one in the same direction, else the overall rate. Room = 0 s.
float rampSeconds(float from, int toRaw, int st)
{
  if (toRaw == TEMP_ROOM) return 0;
  float d = toRaw - from;
  if (fabs(d) < 0.01) return 0;
  RampStep &rs = stats.step[statsIndex(st)];
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

  RampStep &rs = stats.step[statsIndex(PCRstate)];
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
  Serial.print("this run: "); Serial.print(typeName(runType)); Serial.print(runCompleted ? " done" : " stopped");
  Serial.print("  active "); Serial.print(runTotalSecs); Serial.print("s");
#if ENABLE_SQPCR
  Serial.print("  paused "); Serial.print(pausedMs/1000); Serial.print("s  pauses="); Serial.print(pauseCount);
#endif
  Serial.println();
  Serial.print("runs started/completed: "); Serial.print(stats.runsStarted); Serial.print("/"); Serial.println(stats.runsCompleted);
  Serial.print("last run est/actual s: "); Serial.print(stats.lastEstSecs); Serial.print("/"); Serial.println(stats.lastActualSecs);
  Serial.print("overall heat/cool C/s: "); Serial.print(stats.heatRate,3); Serial.print("/"); Serial.print(stats.coolRate,3);
  Serial.print("  ramps: "); Serial.print(stats.rampsHeat); Serial.print("/"); Serial.println(stats.rampsCool);
  for (int i=0;i<8;i++) {
    RampStep &rs = stats.step[i];
    Serial.print(i<5 ? "PCR " : "RT "); Serial.print(i<5 ? stepName(RUN_PCR,i) : stepName(RUN_RT,i-5)); Serial.print(": n="); Serial.print(rs.n);
    Serial.print(" mean="); Serial.print(rs.meanRate,3); Serial.print(" min="); Serial.print(rs.minRate,3);
    Serial.print(" max="); Serial.print(rs.maxRate,3); Serial.print(" C/s  last "); Serial.print(rs.lastDelta,1);
    Serial.print("C in "); Serial.print(rs.lastSecs,1); Serial.println("s");
  }
}

// Estimate of the time left in the run: the rest of the current step, then every remaining step
// (ramp at the learned rate + hold time), following the same order as the PCR state machine.
// stopCycle > 0: stop after the Extension of that cycle (sqPCR "Next Pause").
unsigned long estimateSecondsLeft(int stopCycle)
{
  float secs;
  if (casePCR == PCR_time) secs = (TIMEcontrol > 0) ? TIMEcontrol : 0;
  else secs = (roomStep ? 0 : rampSeconds(temperature_mean, stepTemp(PCRstate), PCRstate)) + stepTime(PCRstate);

  int st = PCRstate, cyc = PCRcycle;
  if ((stopCycle>0)&&(st==3)&&(cyc==stopCycle)) return (unsigned long)secs;
  float prevT = assumedTemp(stepTemp(st));
  while (true) {
    int nx = nextStep(st, cyc);
    if (nx < 0) break;
    st = nx;
    secs += rampSeconds(prevT, stepTempAt(st,cyc), st) + stepTime(st);
    prevT = assumedTemp(stepTempAt(st,cyc));
    if ((stopCycle>0)&&(st==3)&&(cyc==stopCycle)) break;
  }
  return (unsigned long)secs;
}
unsigned long estimateSecondsLeft() { return estimateSecondsLeft(0); }

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
  snprintf(buf, sizeof(buf), "%s %s", typeName(runType), runCompleted ? "Done" : "Stopped");
  drawCentered(2, buf);
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
  if (coolActive) {                       // auto cool-down running: live block temp
    display.setCursor(0,55);
    display.print("Cooling: ");
    display.print(temperature_mean,1);
    display.print("\xA7""C");
  } else {                                // all finished: solid bar like the title
    display.fillRect(0,53,128,11,1);
    display.setTextColor(0);
    drawCentered(55, "COMPLETE");
    display.setTextColor(1);
  }

  display.display();
}

// ---- Program helpers (PCR / RT) ----
bool pcrLike(int type) { return (type==RUN_PCR)||(type==RUN_SQPCR); }
EEprom& prog(int type) { return (type==RUN_SQPCR) ? sqsettings : settings; }   // PCR-like program data
int* progVals(int type) { return (type==RUN_RT) ? rtprog.value : prog(type).value; }
int numSteps(int type) { return (type==RUN_RT) ? 3 : 5; }
int lastValItem(int type) { return (type==RUN_RT) ? 5 : ((type==RUN_SQPCR) ? 13 : 10); }  // then Set, Cancel
bool isTempItem(int type, int item) { return (item%2==0) && !(pcrLike(type)&&(item>=10)); }
const char* typeName(int type) { return (type==RUN_RT) ? "RT" : ((type==RUN_SQPCR) ? "sqPCR" : "PCR"); }
// Program types in Main / Config menu order (sqPCR only when ENABLE_SQPCR)
#if ENABLE_SQPCR
static const int menuTypes[N_TYPES] = {RUN_PCR, RUN_SQPCR, RUN_RT};
#else
static const int menuTypes[N_TYPES] = {RUN_PCR, RUN_RT};
#endif
int menuType(int idx) { return menuTypes[(idx>=0 && idx<N_TYPES) ? idx : 0]; }
int typeMenuIndex(int type) { for (int i=0;i<N_TYPES;i++) if (menuTypes[i]==type) return i; return 0; }
int stepTempAt(int st, int cyc) {          // anneal follows the touchdown schedule
  if (pcrLike(runType)&&(st==2)) return annealTemp(runType, cyc);
  return progVals(runType)[st*2];
}
int stepTemp(int st) { return stepTempAt(st, PCRcycle); }

// ---- Touchdown ----
bool tdOn(int type) { if (!pcrLike(type)) return false; EEprom &P = prog(type);
  return (P.value[4]!=TEMP_ROOM) && (P.tdStart > P.value[4]); }
int tdCycles(int type) { EEprom &P = prog(type);
  return tdOn(type) ? (P.tdStart - P.value[4] + TD_STEP - 1) / TD_STEP : 0; }
int annealTemp(int type, int cyc) {
  EEprom &P = prog(type);
  if (!tdOn(type)) return P.value[4];
  int t = P.tdStart - (cyc-1)*TD_STEP;
  return (t < P.value[4]) ? P.value[4] : t;
}
// Fewest total cycles for a full touchdown: the step-down cycles plus one at the end temp
int minCycles(int type) { return tdOn(type) ? tdCycles(type) + 1 : 1; }
bool isAnnealItem() { return pcrLike(editType) && (MenuItem==4); }
int stepTime(int st) { return progVals(runType)[st*2+1]; }
int totalCycles() { return (runType==RUN_RT) ? 1 : prog(runType).value[10]; }   // total, touchdown included
int statsIndex(int st) { return (runType==RUN_RT) ? 5+st : st; }
float assumedTemp(int raw) { return (raw==TEMP_ROOM) ? ROOM_TEMP_EST : raw; }

const char* stepName(int type, int st)
{
  static const char* pcr[5] = {"Init", "Denature", "Annealing", "Extension", "Final"};
  static const char* rt[3]  = {"Incubation", "Transcribe", "Deactivate"};
  if (type==RUN_RT) return (st>=0 && st<3) ? rt[st] : "";
  return (st>=0 && st<5) ? pcr[st] : "";
}

// Step after st in the running program (cyc is the PCR cycle, updated), or -1 when the run is over
int nextStep(int st, int &cyc)
{
  if (runType==RUN_RT) return (st < 2) ? st+1 : -1;
  if (st==4) return -1;
  if (st==3) { cyc++; return (cyc > totalCycles()) ? 4 : 1; }
  return st+1;
}

// Start a run of the given program type, with all run state reset
void startRun(int type)
{
  runType=type;
  caseUX=CASE_Run; casePCR=PCR_set; PCRstate=0; counter=0; coolInit=false; roomStep=false;
  PCRcycle=1; TEMPi=0; TEMPd=0; PIDIntegration=false; noHeatTiming=false; runStart=millis();
  runCompleted=false; estAtStart=0; stats.runsStarted++;
  runHeatN=0; runCoolN=0; runHeatSecs=0; runCoolSecs=0; runHeatDeg=0; runCoolDeg=0;
  for (int i=0;i<10;i++) TEMPdif_a[i]=0;
  pausedMs=0; pauseCount=0; pauseEditing=false;
  userN=0;
  if (type==RUN_SQPCR) {nextPauseCycle=sqsettings.pauseFirst; pauseN=sqsettings.pauseEvery;}
  else nextPauseCycle=0;
}

// PID bookkeeping (error, integral, 200 ms derivative), shared by running and pausing
void updatePIDTerms()
{
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
  }
}

// Move to the next step, or finish the run
void advanceStep()
{
  int cyc=PCRcycle;
  int nx=nextStep(PCRstate,cyc);
  if (nx<0) {
    caseUX=CASE_Done;
    runCompleted=true;
    stats.runsCompleted++;
    stats.lastEstSecs=estAtStart;
    stats.lastActualSecs=(millis()-runStart-pausedMs)/1000;   // active time, excluding reading pauses
  } else {PCRstate=nx; PCRcycle=cyc; casePCR=PCR_set;}
}

// sqPCR: stop after Extension for a manual reading, holding the Extension temperature
void enterPause()
{
  char buf[12];
  caseUX=CASE_Paused; pauseStartMs=millis(); pauseCount++;
  pauseSel=0; pauseEditing=false; counter=0;
  // Resume [N] default follows the schedule (first -> second, then every), unless changed this run
  if (userN>0) pauseN=userN;
  else if ((pauseCount==1)&&(sqsettings.pauseSecond>PCRcycle)) pauseN=sqsettings.pauseSecond-PCRcycle;
  else pauseN=sqsettings.pauseEvery;
  formatHMS(buf, sizeof(buf), (millis()-runStart)/1000);
  Serial.print("PAUSE cycle="); Serial.print(PCRcycle); Serial.print("/"); Serial.print(totalCycles());
  Serial.print(" elapsed="); Serial.print(buf); Serial.print(" block="); Serial.println(temperature_mean,1);
}

// Leave the pause. next = cycle of the next pause (0 or past the end = no more pauses)
void resumeRun(int next)
{
  pausedMs += millis()-pauseStartMs;
  nextPauseCycle = ((next>0)&&(next<=totalCycles())) ? next : 0;
  Serial.print("RESUME next=");
  if (nextPauseCycle>0) Serial.println(nextPauseCycle); else Serial.println("none");
  caseUX=CASE_Run; counter=0;
  advanceStep();
}

// Pause screen: Resume [N] / Run to completion / Stop run, plus "Holding at <Extension temp>" and time paused
void draw_pause_display()
{
  char buf[24];
  display.clearDisplay();
  display.setFont();
  snprintf(buf, sizeof(buf), "Paused: cycle %d/%d", PCRcycle, totalCycles());
  draw_title(buf);

  snprintf(buf, sizeof(buf), "Resume [%d cycle%s]", pauseN, pauseN==1 ? "" : "s");
  const char* items[3] = {buf, "Run to completion", "Stop run"};
  for (int i=0;i<3;i++) {                   // rows at y 14, 24, 34 (10 px pitch)
    int y = 14 + i*10;
    if (pauseSel==i) {
      if ((i==0)&&pauseEditing) display.drawRect(0,y-1,128,10,1);       // editing N: outline
      else {display.fillRect(0,y-1,128,10,1); display.setTextColor(0);}
    }
    drawCentered(y, items[i]);
    display.setTextColor(1);
  }

  display.drawLine(0,44,127,44,1);
  // Hold Temp: target / block, like the run screen's Temp line
  display.setCursor(0,47);
  display.print("Hold: ");
  display.print((int)(TEMPset+0.5)); display.print("\xA7""C / ");
  display.print(temperature_mean,1); display.print("\xA7""C");
  // Time paused
  unsigned long ps=(millis()-pauseStartMs)/1000;
  formatHMS(buf, sizeof(buf), ps);
  display.setCursor(0,56);
  display.print("Paused: "); display.print(buf);
  display.display();
}

// Annealing bar with touchdown: outline up to the start temp, a line across at the end temp.
// Label shows the start (or the end while editing it); it sits just under the line,
// or above the bar if that would hit the cycles box.
void draw_anneal_box(int x, int y, bool highlight, bool edit)
{
  int start = prog(editType).tdStart;
  int endRaw = prog(editType).value[4];
  int endH = (endRaw==TEMP_ROOM) ? TEMP_ROOM_COUNTER : endRaw;
  int top = y+2-start/2;
  int lineY = y+2-endH/2;

  display.drawRect(x-3,top,17,start/2-1,1);
  display.drawLine(x-3,lineY,x+13,lineY,1);

  bool showEnd = edit && highlight && (annealPhase==1);
  int ly = lineY+2;
  if (ly+8 > 44) ly = top-10;               // keep clear of the cycles box (y 45-55)

  if (highlight&&!edit) {display.fillRect(x-3,ly-1,17,10,1); display.setTextColor(0);}
  if (highlight&&edit) display.drawRect(x-2,ly-1,15,10,1);
  display.setCursor(x,ly);
  if (showEnd) { if (endRaw==TEMP_ROOM) display.print("RT"); else display.print(endRaw); }
  else display.print(start);
  display.setTextColor(1);
}

// sqPCR editor: keep first <= cycles and second after first (or pulled to the cycle count)
void clampPauses()
{
  int cyc = sqsettings.value[10];
  if (sqsettings.pauseFirst > cyc) sqsettings.pauseFirst = cyc;
  if (sqsettings.pauseSecond <= sqsettings.pauseFirst) sqsettings.pauseSecond = sqsettings.pauseFirst+1;
  if (sqsettings.pauseSecond > PAUSE_MAX_N) sqsettings.pauseSecond = PAUSE_MAX_N;
  if (sqsettings.pauseSecond > cyc) sqsettings.pauseSecond = cyc;
}

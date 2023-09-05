// This program allows the TeensyPoly41 board to be used as a dumb MIDI controller.
// Hardware-wise, this has a couple implications:
// 1. The audio output board and 1/4" jacks become superfluous and can be omitted if desired.
// 2. The DIN jack used for MIDI in needs to be rewired if it's going to be used for MIDI output.
//    What I actually ended up doing was throwing another DIN jack and a couple resistors on a
//    breadboard and running wires out. Less intrusive and more reversible.
//    USB could be used for MIDI instead, if the host supports it.


// #include <SPI.h>
#include <Bounce2.h> // for buttons
// #include <Math.h>    // for uh something I'm sure
#include <MIDI.h>    // for MIDI (might want to also add USB midi later)


// todo: comment
#define NUM_LEDS        8
// todo: comment
#define NUM_BUTTONS     8
// todo: comment
#define NUM_POTS        32 // or however many

// Indices for pots[][]
#define POT_VALUE_NEW   0
#define POT_VALUE_OLD   1


typedef enum PotLabels {
  pot_mainVol,    // CC 7
  pot_portTime,   // CC 5
  pot_oscAVol,    // CC 
  pot_crossMod,   // CC 
  pot_subVol,     // CC 
  pot_oscBVol,    // CC 
  pot_oscBTune,   // CC 
  pot_oscCVol,    // CC 

  pot_oscCShape,  // CC 
  pot_oscCTune,   // CC 
  pot_filtCutoff, // CC 74
  pot_filtRes,    // CC 71
  pot_filtEnvAmt, // CC 
  pot_filtA,      // CC 
  pot_filtD,      // CC 
  pot_filtS,      // CC 
  
  pot_filtR,      // CC 
  pot_envA,       // CC 73
  pot_envD,       // CC 80
  pot_envS,       // CC 
  pot_envR,       // CC 72
  pot_lfoFreq,    // CC 
  pot_lfoDepth,   // CC 
  pot_lfoA,       // CC 
  
  pot_lfoD,       // CC 
  pot_lfoS,       // CC 
  pot_pwmFreq,    // CC 
  pot_pwmDepth,   // CC 
  pot_revAmt,     // CC 91
  pot_revSize,    // CC 
  pot_dlyAmt,     // CC 
  pot_dlyRate,    // CC 

  NUM_POT_LABELS
} PotLabels;

// MIDI channel selection pins
const int ChSel1 = 29;
const int ChSel2 = 30;
const int ChSel4 = 31;
const int ChSel8 = 32;

int midiChannel;


// Mux control pins, shared among all mux chips.
const int MUX_A = 8;
const int MUX_B = 9;
const int MUX_C = 10;

// Mux X (common) pins, one for each mux chip.
const int MUX0_X = A0; // pots
const int MUX1_X = A1; // pots
const int MUX2_X = A2; // pots
const int MUX3_X = A3; // pots
const int MUX4_X = 5;  // switches
const int MUX5_X = 6;  // switches
const int MUX6_X = 22; // mem buttons
const int MUX7_X = 23; // LED outputs

// States of muxed LEDs.
int led[NUM_LEDS] = {0,0,0,0,0,0,0,0};

// There are also two other LEDs that don't go through the mux.
const int LED_MEM_ACTIVE = 11;
const int LED_MEM_STORE = 12;

// States of non-muxed LEDs.
int memActive = 0;
int memStore = 0;

// Debounced button states.
Bounce* btns[NUM_BUTTONS];


MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);


unsigned long muxInterval = 4; // Time in ms between mux reads. Glitches happen if this is zero.
unsigned long prevMuxTimer;
int curMuxCh = 0;


// todo: add crap here

// Threshold for the pretty basic algo we use to "debounce" the pots.
const int potThreshold = 2;

int pots[NUM_POT_LABELS][2]; // Note: use POT_VALUE_OLD/NEW for the second index


// Switch input states. For 3-way switches, both up and down have states.
int sw_mainOctHi;
int sw_mainOctLo;
int sw_oscAShapeHi;
int sw_oscAShapeLo;
int sw_oscBOctHi;
int sw_oscBOctLo;
int sw_filtMode;
int sw_isMonophonic;
int sw_oscCOctHi;
int sw_oscCOctLo;
int sw_oscBShapeHi;
int sw_oscBShapeLo;
int sw_lfoShapeHi;
int sw_lfoShapeLo;
int sw_lfoDestHi;
int sw_lfoDestLo;

// This is apparently needed in order to get Serial.println() to behave more usefully.
String str;

uint8_t firstTime = 2;


void muxRead(void);
void potsDebounce(void);

//////////////////////////////////// Functions ///////////////////////////////////////


void setup() {

  // Get serial set up.
  Serial.begin(9600);
  Serial.println("controller_tp41 serial is up.");

  // Get MIDI set up.
  MIDI.begin(MIDI_CHANNEL_OMNI); // todo: verify that this is even necessary

  // Get muxes set up.
  pinMode(MUX_A, OUTPUT);
  pinMode(MUX_B, OUTPUT);
  pinMode(MUX_C, OUTPUT);

  digitalWrite(MUX_A, 0);
  digitalWrite(MUX_B, 0);
  digitalWrite(MUX_C, 0);

  pinMode(MUX0_X, INPUT_DISABLE); // Prevent weird read issues with the ADC. Needed on Teensy 4.1.
  pinMode(MUX1_X, INPUT_DISABLE);
  pinMode(MUX2_X, INPUT_DISABLE);
  pinMode(MUX3_X, INPUT_DISABLE);
  pinMode(MUX4_X, INPUT_PULLUP);
  pinMode(MUX5_X, INPUT_PULLUP);
  pinMode(MUX6_X, INPUT_PULLUP);
  pinMode(MUX7_X, OUTPUT);
  
  digitalWrite(MUX7_X, 0);

  // Set up debouncing.
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    btns[i] = new Bounce();
    btns[i]->attach(MUX6_X, INPUT_PULLUP);
    btns[i]->interval(20);
  }

  // Get non-muxed LEDs set up.
  pinMode(LED_MEM_ACTIVE, OUTPUT);
  pinMode(LED_MEM_STORE, OUTPUT);

  digitalWrite(LED_MEM_ACTIVE, 0);
  digitalWrite(LED_MEM_STORE, 0);

  // Smooth out the analog reads a bit.
  analogReadAveraging(32);

  // Set up MIDI channel select pins. This means we need a Teensy 4.1; a 4.0 lacks these pins.
  pinMode(ChSel1, INPUT_PULLUP);
  pinMode(ChSel2, INPUT_PULLUP);
  pinMode(ChSel4, INPUT_PULLUP);
  pinMode(ChSel8, INPUT_PULLUP);
}



void loop() {

  // Read pots and stuff.
  if (millis() - prevMuxTimer > muxInterval) {
    prevMuxTimer = millis();
    muxRead();
  }

  // Read/debounce buttons?

  // Debounce pots and send MIDI messages if appropriate.
  potsDebounce();
}




void midiTx(uint16_t txNumber, uint16_t txValue) {

  // Strategy: MIDI CC messages.
  // Advantages: simple, easy, fast.
  // Disadvantages: less flexible, only 7-bit resolution.
  uint8_t controlNumber = txNumber; // this'll go with each slider, I guess hardcoded for now
  uint8_t controlValue = ((txValue >> 3) & 0x7F); // convert from 10 to 7 bits
  MIDI.sendControlChange(controlNumber, controlValue, midiChannel);

  return; // lol debug: We'll just use CC messages for today even if it's a noncompliant usage.

  // Strategy: MIDI NRPN messages.
  // Advantages: 14-bit resolution.
  // Disadvantages: much slower.
  uint16_t nrpnNumber = 0;
  uint16_t nrpnValue = 0;
  MIDI.beginNrpn(nrpnNumber, midiChannel);
  MIDI.sendNrpnValue(nrpnValue, midiChannel);
  MIDI.endNrpn(midiChannel);
}

// Read pots and switches, write LEDs
void muxRead(void) {
  // I feel like there might be a nicer way to do this.

  if (false) // Something like this?
  {
    pots[curMuxCh]     [0] = analogRead(MUX0_X);
    pots[curMuxCh + 8] [0] = analogRead(MUX1_X);
    pots[curMuxCh + 16][0] = analogRead(MUX2_X);
    pots[curMuxCh + 24][0] = analogRead(MUX3_X);
  }

  switch (curMuxCh) {
  case 0:
    pots[pot_mainVol][0] = analogRead(MUX0_X);
    pots[pot_oscCShape][0] = analogRead(MUX1_X);
    pots[pot_filtR][0] = analogRead(MUX2_X);
    pots[pot_lfoD][0] = analogRead(MUX3_X);

    sw_mainOctLo = digitalRead(MUX4_X);
    sw_oscCOctLo = digitalRead(MUX5_X);

    // todo: buttons ig

    // X1
    digitalWrite(MUX_A, 1);
    digitalWrite(MUX_B, 0);
    digitalWrite(MUX_C, 0);
    
    digitalWrite(MUX7_X, (led[1] ? 1 : 0));

    // Suppress a big fat flood of MIDI messages at boot.
    if (firstTime) { firstTime--; }
    break;

  case 1:
    pots[pot_portTime][0] = analogRead(MUX0_X);
    pots[pot_oscCTune][0] = analogRead(MUX1_X);
    pots[pot_envA][0] = analogRead(MUX2_X);
    pots[pot_lfoS][0] = analogRead(MUX3_X);
    
    sw_oscAShapeHi = digitalRead(MUX4_X);
    sw_oscBShapeHi = digitalRead(MUX5_X);

    // X2
    digitalWrite(MUX_A, 0);
    digitalWrite(MUX_B, 1);
    digitalWrite(MUX_C, 0);
    
    digitalWrite(MUX7_X, (led[2] ? 1 : 0));
    break;

  case 2:
    pots[pot_oscAVol][0] = analogRead(MUX0_X);
    pots[pot_filtCutoff][0] = analogRead(MUX1_X);
    pots[pot_envD][0] = analogRead(MUX2_X);
    pots[pot_pwmFreq][0] = analogRead(MUX3_X);

    sw_oscAShapeLo = digitalRead(MUX4_X);
    sw_oscBShapeLo = digitalRead(MUX5_X);

    // X3
    digitalWrite(MUX_A, 1);
    digitalWrite(MUX_B, 1);
    digitalWrite(MUX_C, 0);
    
    digitalWrite(MUX7_X, (led[3] ? 1 : 0));
    break;

  case 3:
    pots[pot_crossMod][0] = analogRead(MUX0_X);
    pots[pot_filtRes][0] = analogRead(MUX1_X);
    pots[pot_envS][0] = analogRead(MUX2_X);
    pots[pot_pwmDepth][0] = analogRead(MUX3_X);

    sw_mainOctHi = digitalRead(MUX4_X);
    sw_oscCOctHi = digitalRead(MUX5_X);

    // X4
    digitalWrite(MUX_A, 0);
    digitalWrite(MUX_B, 0);
    digitalWrite(MUX_C, 1);
    
    digitalWrite(MUX7_X, (led[4] ? 1 : 0));
    break;

  case 4:
    pots[pot_subVol][0] = analogRead(MUX0_X);
    pots[pot_filtEnvAmt][0] = analogRead(MUX1_X);
    pots[pot_envR][0] = analogRead(MUX2_X);
    pots[pot_revAmt][0] = analogRead(MUX3_X);

    sw_oscBOctLo = digitalRead(MUX4_X);
    sw_lfoShapeLo = digitalRead(MUX5_X);

    // X5
    digitalWrite(MUX_A, 1);
    digitalWrite(MUX_B, 0);
    digitalWrite(MUX_C, 1);
    
    digitalWrite(MUX7_X, (led[5] ? 1 : 0));
    break;

  case 5:
    pots[pot_oscBVol][0] = analogRead(MUX0_X);
    pots[pot_filtA][0] = analogRead(MUX1_X);
    pots[pot_lfoFreq][0] = analogRead(MUX2_X);
    pots[pot_revSize][0] = analogRead(MUX3_X);

    sw_isMonophonic = digitalRead(MUX4_X);
    sw_lfoDestHi = digitalRead(MUX5_X);

    // X6
    digitalWrite(MUX_A, 0);
    digitalWrite(MUX_B, 1);
    digitalWrite(MUX_C, 1);
    
    digitalWrite(MUX7_X, (led[6] ? 1 : 0));
    break;

  case 6:
    pots[pot_oscBTune][0] = analogRead(MUX0_X);
    pots[pot_filtD][0] = analogRead(MUX1_X);
    pots[pot_lfoDepth][0] = analogRead(MUX2_X);
    pots[pot_dlyAmt][0] = analogRead(MUX3_X);

    sw_oscBOctHi = digitalRead(MUX4_X);
    sw_lfoShapeHi = digitalRead(MUX5_X);

    // X7
    digitalWrite(MUX_A, 1);
    digitalWrite(MUX_B, 1);
    digitalWrite(MUX_C, 1);
    
    digitalWrite(MUX7_X, (led[7] ? 1 : 0));
    break;

  case 7:
    pots[pot_oscCVol][0] = analogRead(MUX0_X);
    pots[pot_filtS][0] = analogRead(MUX1_X);
    pots[pot_lfoA][0] = analogRead(MUX2_X);
    pots[pot_dlyRate][0] = analogRead(MUX3_X);

    sw_filtMode = digitalRead(MUX4_X);
    sw_lfoDestLo = digitalRead(MUX5_X);

    digitalWrite(MUX_A, 0); //A
    digitalWrite(MUX_B, 0); //B
    digitalWrite(MUX_C, 0); //C

    digitalWrite(MUX7_X, (led[0] ? 1 : 0));
    break;

  default:
    curMuxCh = 7;
    break;
  } // end switch

  curMuxCh++;
  if (curMuxCh > 7) { curMuxCh = 0; }


  // Read the MIDI channel select pins.
  // Do we need to do this quite so often? Probably not.
  // Do I care? Also probably not.
  midiChannel = 0;
  midiChannel += (digitalRead(ChSel1) ? 0 : 1);
  midiChannel += (digitalRead(ChSel2) ? 0 : 2);
  midiChannel += (digitalRead(ChSel4) ? 0 : 4);
  midiChannel += (digitalRead(ChSel8) ? 0 : 8);
  // There's a 0 position on the dial, but not a MIDI channel 0.
  if (midiChannel == 0) { midiChannel = 1; }
}

void potsDebounce(void) {
  // Go through each pot. If it's moved far enough since the last time, do the thing.
  for (uint8_t p = 0; p < NUM_POTS; p++) {
    if ((pots[p][POT_VALUE_OLD] + potThreshold < pots[p][POT_VALUE_NEW]) ||
        (pots[p][POT_VALUE_OLD] - potThreshold > pots[p][POT_VALUE_NEW])) {
      pots[p][POT_VALUE_OLD] = pots[p][POT_VALUE_NEW];
      // todo: Divide it down (if necessary)
      // todo: Add it to the queue or otherwise mark it somehow.

      if (!firstTime) {
        Serial.println(str + "ch" + midiChannel + ", pot " + p + " turn, new value " + pots[p][POT_VALUE_NEW]);
        midiTx(p, pots[p][POT_VALUE_NEW]);
      }
    }
  }
}

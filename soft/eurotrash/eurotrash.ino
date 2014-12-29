/*
*   eurotrash
*   dual wav player (test version).
*
*   files should be 16 bit stereo, 44.1kHz; file names 8.3 (SFN). 
*   max files = 128 (can be changed - see the respective #define (MAXFILES)
*   a/the list of valid files will be generated during initialization.
*
*   micro SD card should be class 10.
*
*   to do:
*   - calibrate ADC / save to EEPROM
*   - move to SD raw/mono 
*   - open files by index
*   - move to ADC/DMA
*/


#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <rotaryplus.h>  // used for the encoders. the standard/official <Encoder> library doesn't seem to work properly here

#define HWSERIAL Serial1 // >> atmega328, expected baudrate is 115200
#define BAUD 115200
File root;

/* ---------------------- Audio API --------------------- */

AudioPlaySdWav           wav1, wav2, wav3, wav4;
AudioPlaySdWav           *wav[4] = {&wav1, &wav2, &wav3, &wav4};
AudioEffectFade          fade1, fade2, fade3, fade4;
AudioEffectFade          *fade[4] = {&fade1, &fade2, &fade3, &fade4};
AudioMixer4              mixL, mixR;
AudioOutputI2S           dac;   

AudioConnection          link_0(wav1, 0, fade1, 0);
AudioConnection          link_1(wav2, 0, fade2, 0);
AudioConnection          link_2(wav3, 0, fade3, 0);
AudioConnection          link_3(wav4, 0, fade4, 0);

AudioConnection          link_4(fade1, 0, mixL, 0);
AudioConnection          link_5(fade2, 0, mixL, 1);
AudioConnection          link_6(fade3, 0, mixR, 0);
AudioConnection          link_7(fade4, 0, mixR, 1);
AudioConnection          link_8(mixL, 0, dac, 0);
AudioConnection          link_9(mixR, 0, dac, 1);

/* ------------------------- pins ------------------------ */

#define CLK_L 2
#define CLK_R 0

#define EOF_L 20
#define EOF_R 21

#define CV1 16
#define CV2 17
#define CV3 18
#define CV4 19

#define BUTTON_R 15
#define BUTTON_L 5 
#define ENC_L1 4
#define ENC_L2 3
#define ENC_R1 8 
#define ENC_R2 6 

/* CV inputs */
#define numADC 4
#define ADC_RES 12

int16_t CV[numADC];
uint8_t ADC_cycle;
uint16_t HALFSCALE = 0; 

/* encoders */ 
Rotary encoder[2] = {{ENC_L1, ENC_L2}, {ENC_R1, ENC_R2}}; 


/* ----------------------- timers + ISR stuff ------------------------ */

volatile uint8_t LCLK;   
volatile uint8_t RCLK;
volatile uint8_t BUTTON;
uint32_t LASTBUTTON;
const uint16_t DEBOUNCE = 400;

void CLK_ISR_L() { LCLK = true; }
void CLK_ISR_R() { RCLK = true; }
void BUTTON_ISR_L() { BUTTON = 0x01; }
void BUTTON_ISR_R() { BUTTON = 0x02; }

IntervalTimer UI_timer, ADC_timer;
volatile uint8_t UI  = false;
volatile uint8_t _ADC = false;

#define UI_RATE  15000  // UI update rate
#define ADC_RATE 10000  // ADC sampling rate (*4)
void UItimerCallback()  { UI = true;  }
void ADCtimerCallback() { _ADC = true; }


/* ----------------------- output channels --------------- */

#define CHANNELS 2
#define LEFT  0
#define RIGHT 1
#define INIT_FILE 0

typedef struct audioChannel {
  
    uint8_t     id;            // channel L/R
    uint8_t     file_wav;      // fileSelect
    uint32_t    pos0;          // file start pos
    uint32_t    pos1;          // end pos
    uint32_t    ctrl_res;      // start pos resolution (in bytes)
    uint32_t    ctrl_res_eof;  // eof resolution  (in ms) 
    float       _gain;         // volume 
    uint32_t     eof;          // end of file (in ms)
    uint8_t     swap;          // ping-pong file (1/2; 3/4)

} audioChannel;

struct audioChannel *audioChannels[CHANNELS];

const uint8_t  FADE_IN  = 20;   // fade in  (adjust to your liking)
const uint16_t FADE_OUT = 200;  // fade out (ditto)
uint8_t  FADE_LEFT, FADE_RIGHT, EOF_L_OFF, EOF_R_OFF;
uint32_t last_LCLK, last_RCLK, last_EOF_L, last_EOF_R;
const uint8_t TRIG_LENGTH = 25; // trig length / clock out


/* ------------------------------------------------------ */

void setup() {
 
  //analogReference(EXTERNAL);
  analogReadRes(ADC_RES);
  analogReadAveraging(16);   
  /* clk inputs and switches need the pullups */
  pinMode(CLK_L, INPUT_PULLUP);
  pinMode(CLK_R, INPUT_PULLUP);
  pinMode(BUTTON_L, INPUT_PULLUP); 
  pinMode(BUTTON_R, INPUT_PULLUP); 
  
  pinMode(EOF_L, OUTPUT);
  pinMode(EOF_R, OUTPUT);
  
  digitalWrite(EOF_L, LOW);
  digitalWrite(EOF_R, LOW);
  
  /* audio API */
  AudioMemory(15);

  SPI.setMOSI(7);
  SPI.setSCK(14);
  if (!(SD.begin(10))) {
    
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
  delay(100);
  /* zero volume while we generate the file list */
  mixL.gain(0, 0);
  mixL.gain(1, 0);
  mixR.gain(0, 0);
  mixR.gain(1, 0);
  
  generate_file_list();
  /* begin timers and HW serial */
  ADC_timer.begin(ADCtimerCallback, ADC_RATE); 
  
  HWSERIAL.begin(BAUD);
  HWSERIAL.print('\n');
  delay(10);
  
  UI_timer.begin(UItimerCallback, UI_RATE);
  
  /* allocate memory for L/R + init */
  audioChannels[LEFT]  = (audioChannel*)malloc(sizeof(audioChannel));
  audioChannels[RIGHT] = (audioChannel*)malloc(sizeof(audioChannel));
  init_channels(INIT_FILE);
  
  /* set volume */
  mixL.gain(0, audioChannels[LEFT]->_gain);
  mixL.gain(1, audioChannels[LEFT]->_gain);
  mixR.gain(0, audioChannels[RIGHT]->_gain);
  mixR.gain(1, audioChannels[RIGHT]->_gain);
  
  attachInterrupt(CLK_L, CLK_ISR_L, FALLING);
  attachInterrupt(CLK_R, CLK_ISR_R, FALLING);
  attachInterrupt(BUTTON_L, BUTTON_ISR_L, FALLING);
  attachInterrupt(BUTTON_R, BUTTON_ISR_R, FALLING);
  
  attachInterrupt(ENC_L1, left_encoder_ISR, CHANGE);
  attachInterrupt(ENC_L2, left_encoder_ISR, CHANGE);
  attachInterrupt(ENC_R1, right_encoder_ISR, CHANGE);
  attachInterrupt(ENC_R2, right_encoder_ISR, CHANGE);
  
  delay(10);
   /*  calibrate mid point ? */
  if (!digitalRead(BUTTON_L)) calibrate(); 
  else if (EEPROM.read(0x0)==0xFF) HALFSCALE = readMIDpoint();
  else HALFSCALE = pow(2,ADC_RES-1)-1;
  
  update_display(LEFT,  INIT_FILE);
  update_display(RIGHT, INIT_FILE);
}

/* main loop, wherein we mainly wait for the clock-flags */

void loop() {
 
   leftright();

   /* eof left? */
   
   if (!FADE_LEFT && ((millis() - last_LCLK > audioChannels[LEFT]->eof))) {
        
        digitalWriteFast(EOF_L, HIGH);  
        fade1.fadeOut(FADE_OUT); // to do: we only need to fade out the file that's playing
        fade2.fadeOut(FADE_OUT);      
        last_EOF_L = millis();
        EOF_L_OFF = FADE_LEFT = true;
     
   } 
       
   leftright();
   
   /* eof right? */
   
   if (!FADE_RIGHT && ((millis() - last_RCLK > audioChannels[RIGHT]->eof))) {
        
        digitalWriteFast(EOF_R, HIGH);
        fade3.fadeOut(FADE_OUT);
        fade4.fadeOut(FADE_OUT);
        last_EOF_R = millis();
        EOF_R_OFF = FADE_RIGHT = true;
   }
   
   leftright();
   
   if (UI) {
       update_enc();
       UI = false;
   }
   
   leftright();
   
   if (BUTTON && (millis() - LASTBUTTON > DEBOUNCE)) {
       buttons(BUTTON-0x01);
       BUTTON = false;  
       LASTBUTTON = millis();  
   } 
   
   leftright();
   
   if (_ADC) {
   
       _ADC = false;
       ADC_cycle++;
       if (ADC_cycle >= numADC)  ADC_cycle = 0; 
       CV[ADC_cycle] = analogRead(ADC_cycle+0x10);
       update_eof(ADC_cycle);
        
       /*if (!ADC_cycle) Serial.println(" ||| ");
       else Serial.print(" || ");
       Serial.print(CV[ADC_cycle]);
       Serial.print(" -> ");
       Serial.print(ADC_cycle);
       */
   }  
   
   leftright();
    
   if (EOF_L_OFF && (millis() - last_EOF_L > TRIG_LENGTH))  { digitalWriteFast(EOF_L, LOW); EOF_L_OFF = false; }
   
   leftright();
   
   if (EOF_R_OFF && (millis() - last_EOF_R > TRIG_LENGTH))  { digitalWriteFast(EOF_R, LOW); EOF_R_OFF = false; }
}

/* ------------------------------------------------------------ */



/*
*   eurotrash
*   test 1
*
*/


#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
//#include <Encoder.h>
#include <rotaryplus.h>

#define HWSERIAL Serial1
File root;

/* ----------------------Audio API ---------------------- */

AudioPlaySdWav           wav1, wav2, wav3, wav4;
AudioPlaySdWav           *wav[4] = {&wav1, &wav2, &wav3, &wav4};
AudioEffectFade          fade1, fade2, fade3, fade4;
AudioEffectFade          *fade[4] = {&fade1, &fade2, &fade3, &fade4};
AudioMixer4              mix;
AudioOutputI2S           dac;   

AudioConnection          patchCord1(wav1, 0, fade1, 0);
AudioConnection          patchCord2(wav2, 0, fade2, 0);
AudioConnection          patchCord3(wav3, 0, fade3, 0);
AudioConnection          patchCord4(wav4, 0, fade4, 0);

AudioConnection          patchCord5(fade1, 0, mix, 0);
AudioConnection          patchCord6(fade2, 0, mix, 1);
AudioConnection          patchCord7(fade3, 0, mix, 2);
AudioConnection          patchCord8(fade4, 0, mix, 3);
AudioConnection          patchCord9(mix, 0, dac, 0);

/* ------------------------- pins ------------------------ */

#define CLK_L 2
#define CLK_R 0

#define EOL_L 20
#define EOL_R 21

#define CV1 A2
#define CV2 A3
#define CV3 A4
#define CV4 A5

#define BUTTON_R 15
#define BUTTON_L 5 
#define ENC_L1 4
#define ENC_L2 3
#define ENC_R1 8 
#define ENC_R2 6 

Rotary encoder[2] = {{ENC_L1, ENC_L2}, {ENC_R1, ENC_R2}}; 

uint16_t CVs[] = {CV1, CV2, CV3, CV4};
uint8_t ADC_cycle;
const uint8_t numADC = 4;

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
volatile uint8_t ADC = false;

void UItimerCallback()  { UI = true;  }
void ADCtimerCallback() { ADC = true; }


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
    uint32_t    file_len;      // length in bytes
    uint32_t    ctrl_res;      // play head resolution ( -> encoders)
    uint32_t    ctrl_res_eof;  // eof resolution ( -> encoders)
    float       _gain;         // volume 
    uint32_t     eof;          // end of file
    //uint8_t     mode;        // one-shot / loop [not now] 
    uint8_t     swap;          // ping-pong file (1/2; 3/4)

} audioChannel;

struct audioChannel *audioChannels[CHANNELS];

const uint8_t  FADE_IN  = 20;
const uint16_t FADE_OUT = 200;
uint8_t  FADE_LEFT  = true;
uint8_t  FADE_RIGHT = true;
uint32_t last_LCLK, last_RCLK;


/* ------------------------------------------------------ */

void setup() {
 
  analogReadRes(12);
  analogReadAveraging(16);   
 
  pinMode(CLK_L, INPUT_PULLUP);
  pinMode(CLK_R, INPUT_PULLUP);
  pinMode(BUTTON_L, INPUT_PULLUP); 
  pinMode(BUTTON_R, INPUT_PULLUP); 
  
  pinMode(EOL_L, OUTPUT);
  pinMode(EOL_R, OUTPUT);
 
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
  
  mix.gain(0, 0);
  mix.gain(1, 0);
  mix.gain(2, 0);
  mix.gain(3, 0);
  
  generate_file_list();

  ADC_timer.begin(ADCtimerCallback, 15000); 
  
  HWSERIAL.begin(115200);
  HWSERIAL.print('\n');
  delay(10);
  
  UI_timer.begin(UItimerCallback, 15000);
  
  // allocate memory for L/R
  audioChannels[LEFT]  = (audioChannel*)malloc(sizeof(audioChannel));
  audioChannels[RIGHT] = (audioChannel*)malloc(sizeof(audioChannel));
  init_channels(INIT_FILE);
  
  mix.gain(0, audioChannels[LEFT]->_gain);
  mix.gain(1, audioChannels[LEFT]->_gain);
  mix.gain(2, audioChannels[RIGHT]->_gain);
  mix.gain(3, audioChannels[RIGHT]->_gain);
  
  attachInterrupt(CLK_L, CLK_ISR_L, FALLING);
  attachInterrupt(CLK_R, CLK_ISR_R, FALLING);
  attachInterrupt(BUTTON_L, BUTTON_ISR_L, FALLING);
  attachInterrupt(BUTTON_R, BUTTON_ISR_R, FALLING);
  
  attachInterrupt(ENC_L1, left_encoder_ISR, CHANGE);
  attachInterrupt(ENC_L2, left_encoder_ISR, CHANGE);
  attachInterrupt(ENC_R1, right_encoder_ISR, CHANGE);
  attachInterrupt(ENC_R2, right_encoder_ISR, CHANGE);
  
  update_display(LEFT,  INIT_FILE);
  update_display(RIGHT, INIT_FILE);

}


void loop() {
  
    if (LCLK) {  
     
       play_x(LEFT);
       LCLK = false;
       FADE_LEFT = false;
       last_LCLK = millis();
 
   } 
   
   if (RCLK) { 
     
       play_x(RIGHT);
       RCLK = false;
       FADE_RIGHT = false;
       last_RCLK = millis();
 
   }  
  
   /*if (millis() - last_LCLK > 1000) {
     
       LCLK = true;
       
   }*/
   
   // eof left?
   
   if (!FADE_LEFT && ((millis() - last_LCLK > audioChannels[LEFT]->eof))) {
        
        fade1.fadeOut(FADE_OUT);
        fade2.fadeOut(FADE_OUT);
        FADE_LEFT = true;
     
   } 
       
   if (LCLK) {  
     
       play_x(LEFT);
       LCLK = false;
       FADE_LEFT = false;
       last_LCLK = millis();
 
   } 
   if (RCLK) { 
     
       play_x(RIGHT);
       RCLK = false;
       FADE_RIGHT = false;
       last_RCLK = millis();
 
   } 
   
   // eof right?
   
   if (!FADE_RIGHT && ((millis() - last_RCLK > audioChannels[RIGHT]->eof))) {
        
        fade3.fadeOut(FADE_OUT);
        fade4.fadeOut(FADE_OUT);
        FADE_RIGHT = true;
     
   }
   
   if (LCLK) {  
     
       play_x(LEFT);
       LCLK = false;
       FADE_LEFT = false;
       last_LCLK = millis();
 
   } 
   if (RCLK) { 
     
       play_x(RIGHT);
       RCLK = false;
       FADE_RIGHT = false;
       last_RCLK = millis();
 
   } 
   
   if (UI) {
       update_enc();
       UI = false;
   }
   
   if (LCLK) {  
     
       play_x(LEFT);
       LCLK = false;
       FADE_LEFT = false;
       last_LCLK = millis();
 
   } 
   if (RCLK) { 
     
       play_x(RIGHT);
       RCLK = false;
       FADE_RIGHT = false;
       last_RCLK = millis();
 
   } 
   
   if (BUTTON && (millis() - LASTBUTTON > DEBOUNCE)) {
       buttons(BUTTON-0x01);
       BUTTON = false;  
       LASTBUTTON = millis();  
   } 
   
   if (LCLK) {  
  
       play_x(LEFT);
       LCLK = false;
       FADE_LEFT = false;
       last_LCLK = millis();
 
   } 
   if (RCLK) { 
 
       play_x(RIGHT);
       RCLK = false;
       FADE_RIGHT = false;
       last_RCLK = millis();
 
   } 
   
   if (ADC) {
   
       ADC = false;
       ADC_cycle++;
       if (ADC_cycle >= numADC) ADC_cycle = 0;
       uint16_t x = analogRead(CVs[ADC_cycle]); 
       //Serial.println(x);
   }  
}

/* ------------------------------------------------------------ */


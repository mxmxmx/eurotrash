/*
*   eurotrash
*   dual wav player (test version).
*
*   - wav files should be 16 bit stereo or mono, 44.1kHz; **file names need to be 8.3** (SFN). 
*   max files = 128 (can be changed - see the respective #define (MAXFILES)
*   a/the list of valid files will be generated during initialization.
*
*   micro SD card should be *class 10*.
*
*   - 'raw' files that go on the flash need to be stored in a folder called /SERFLASH
*   technically, they're not simply raw data; ie they *must* be created with wav2raw.c 
*
*   - TD: fix SPIFIFO for CS = 13
*   - TD: move eof to bytes
*   - TD: spi flash parsing -> char[]
*/

//#define REV1 

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <rotaryplus.h>  // used for the encoders. the standard/official <Encoder> library doesn't seem to work properly here
#include <play_rawflash13.h>


#define HWSERIAL Serial1 // >> atmega328, expected baudrate is 115200
#define BAUD 115200
File root;

/* ---------------------- Audio API --------------------- */

AudioPlaySdWav           wav1, wav2, wav3, wav4;
AudioPlaySdWav           *wav[4] = {&wav1, &wav2, &wav3, &wav4};
AudioPlaySerialFlash     raw1, raw2, raw3, raw4;
AudioPlaySerialFlash     *raw[4] = {&raw1, &raw2, &raw3, &raw4};
AudioEffectFade          fade1, fade2, fade3, fade4, fade1r, fade2r, fade3r, fade4r;
AudioEffectFade          *fade[8] = {&fade1, &fade2, &fade3, &fade4, &fade1r, &fade2r, &fade3r, &fade4r};
AudioMixer4              mixL, mixR;
AudioOutputI2S           pcm5102a;   

AudioConnection          ac_0(wav1, 0, fade1, 0);
AudioConnection          ac_1(wav2, 0, fade2, 0);
AudioConnection          ac_2(wav3, 0, fade3, 0);
AudioConnection          ac_3(wav4, 0, fade4, 0);

AudioConnection          ac_0r(raw1, 0, fade1r, 0);
AudioConnection          ac_1r(raw2, 0, fade2r, 0);
AudioConnection          ac_2r(raw3, 0, fade3r, 0);
AudioConnection          ac_3r(raw4, 0, fade4r, 0);

AudioConnection          ac_4(fade1,   0, mixL, 0);
AudioConnection          ac_5(fade2,   0, mixL, 1);
AudioConnection          ac_6(fade1r,  0, mixL, 2);
AudioConnection          ac_7(fade2r,  0, mixL, 3);
AudioConnection          ac_8(fade3,   0, mixR, 0);
AudioConnection          ac_9(fade4,   0, mixR, 1);
AudioConnection          ac_10(fade3r, 0, mixR, 2);
AudioConnection          ac_11(fade4r, 0, mixR, 3);

AudioConnection          ac_12(mixL, 0, pcm5102a, 0);
AudioConnection          ac_13(mixR, 0, pcm5102a, 1);

/* ----------------------- output channels --------------- */

#define CHANNELS 2
#define LEFT  0
#define RIGHT 1
#define INIT_FILE 0

typedef struct audioChannel {
  
    uint8_t     id;            // channel L/R
    uint8_t     file_wav;      // fileSelect
    uint32_t    pos0;          // file start pos manual
    uint32_t    posX;          // end pos
    uint32_t    srt;           // start pos
    uint32_t    ctrl_res;      // start pos resolution (in bytes)
    uint32_t    ctrl_res_eof;  // eof resolution  (in ms) 
    float       _gain;         // volume 
    uint32_t    eof;           // end of file (in ms)
    uint8_t     swap;          // ping-pong file (1/2; 3/4)
    uint8_t     bank;          // bank: SD / Flash

} audioChannel;

struct audioChannel *audioChannels[CHANNELS];

/* ----------------------- channel misc ------------------- */

const uint8_t  FADE_IN  = 3;       // fade in  (adjust to your liking)
const uint16_t FADE_OUT = 100;     // fade out (ditto)
const uint8_t  FADE_IN_RAW  = 1;   // fade in  / flash
//const uint16_t FADE_OUT_RAW = 70;  // fade out / flash

uint8_t  FADE_LEFT, FADE_RIGHT, EOF_L_OFF, EOF_R_OFF;
uint32_t last_LCLK, last_RCLK, last_EOF_L, last_EOF_R;
const uint8_t TRIG_LENGTH = 25; // trig length / clock out

uint8_t SPI_FLASH_STATUS = 0;

/* ------------------------- pins ------------------------- */

#define CLK_L 2
#define CLK_R 0

#define EOF_L 20
#define EOF_R 21

#define CV1 16
#define CV2 17
#define CV3 18
#define CV4 19

#define BUTTON_L 5 
#define ENC_L1 4
#define ENC_L2 3
#define ENC_R1 8 
#define ENC_R2 6 

#define CS_SD 10   

#ifdef REV1
  #define CS_MEM 15   // rev1
  #define BUTTON_R 13 // rev1
  #define SPI_FLASH 1 // rev1
#else
  #define CS_MEM 13   // rev0
  #define BUTTON_R 15 // rev0
  #define SPI_FLASH 0 // rev0
#endif

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
uint32_t LASTBUTTON;
const uint16_t DEBOUNCE = 250;

void CLK_ISR_L() { LCLK = true; }
void CLK_ISR_R() { RCLK = true; }

IntervalTimer UI_timer, ADC_timer;
volatile uint8_t UI  = false;
volatile uint8_t _ADC = false;

#define UI_RATE  15000  // UI update rate
#define ADC_RATE 250    // ADC sampling rate (*4)
void UItimerCallback()  { UI = true;  }
void ADCtimerCallback() { _ADC = true; }

/* ------------------------------------------------------ */

void setup() {
  
  //while (!Serial) {;}  
  analogReference(EXTERNAL);
  analogReadRes(ADC_RES);
  analogReadAveraging(4);   
  /* clk inputs and switches need the pullups */
  pinMode(CLK_L, INPUT_PULLUP);
  pinMode(CLK_R, INPUT_PULLUP);
  pinMode(BUTTON_L, INPUT_PULLUP); 
  pinMode(BUTTON_R, INPUT_PULLUP); 
  /* clk outputs */
  pinMode(EOF_L, OUTPUT);
  pinMode(EOF_R, OUTPUT);  
  digitalWrite(EOF_L, LOW);
  digitalWrite(EOF_R, LOW);
  #ifndef REV1
      pinMode(CS_MEM, OUTPUT);    
      digitalWriteFast(CS_MEM, HIGH);
  #endif    
  /* audio API */
  AudioMemory(15);
  SPI.setMOSI(7);
  SPI.setSCK(14);
  
  if (!(SD.begin(CS_SD))) {
    
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
  delay(100);
   
  HWSERIAL.begin(BAUD);
  HWSERIAL.print('\n');
  delay(10);
  
  /* zero volume while we generate the file list */
  mixL.gain(0, 0);
  mixL.gain(1, 0);
  mixL.gain(2, 0);
  mixL.gain(3, 0);
  mixR.gain(0, 0);
  mixR.gain(1, 0);
  mixR.gain(2, 0);
  mixR.gain(3, 0);
  
  /*  get wav from SD */
  generate_file_list();
  /* and spi flash */
  if (SPI_FLASH) SPI_FLASH_STATUS = spi_flash_init();
  /*  update spi flash ? */
  if (!digitalRead(BUTTON_R) && SPI_FLASH_STATUS) SPI_FLASH_STATUS = spi_flash(); 
  /*  files on spi flash ? */
  if (SPI_FLASH_STATUS) SPI_FLASH_STATUS = extract();
  /*  file names */
  if (SPI_FLASH_STATUS) generate_file_list_flash();
  
  /* begin timers and HW serial */
  ADC_timer.begin(ADCtimerCallback, ADC_RATE); 
  UI_timer.begin(UItimerCallback, UI_RATE);
  
  /* allocate memory for L/R + init */
  audioChannels[LEFT]  = (audioChannel*)malloc(sizeof(audioChannel));
  audioChannels[RIGHT] = (audioChannel*)malloc(sizeof(audioChannel));
  init_channels(INIT_FILE);
  
    
  attachInterrupt(CLK_L, CLK_ISR_L, FALLING);
  attachInterrupt(CLK_R, CLK_ISR_R, FALLING);
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
 
    /* set volume */
  mixL.gain(0, audioChannels[LEFT]->_gain);
  mixL.gain(1, audioChannels[LEFT]->_gain);
  mixL.gain(2, audioChannels[LEFT]->_gain);
  mixL.gain(3, audioChannels[LEFT]->_gain);
  mixR.gain(0, audioChannels[RIGHT]->_gain);
  mixR.gain(1, audioChannels[RIGHT]->_gain);
  mixR.gain(2, audioChannels[RIGHT]->_gain);
  mixR.gain(3, audioChannels[RIGHT]->_gain);
  //info();
}

/* main loop, wherein we mainly wait for the clock-flags */

void loop() 
{
  
  while(1) {

     leftright();
   
     if (!FADE_LEFT) eof_left();
       
     leftright();
   
     if (!FADE_RIGHT) eof_right();
     
     leftright();
   
     if (UI) _UI();
   
     leftright();
   
     if (_ADC) _adc();
   
     leftright();
    
     if (EOF_L_OFF && (millis() - last_EOF_L > TRIG_LENGTH))  { digitalWriteFast(EOF_L, LOW); EOF_L_OFF = false; }
   
     leftright();
   
     if (EOF_R_OFF && (millis() - last_EOF_R > TRIG_LENGTH))  { digitalWriteFast(EOF_R, LOW); EOF_R_OFF = false; }
  } 
}

/* ------------------------------------------------------------ */



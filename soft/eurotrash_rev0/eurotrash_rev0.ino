/*
*   eurotrash
*   dual wav player. 'beta'
*
*   - wav files should be 16 bit stereo or mono, 44.1kHz; **file names need to be 8.3** (SFN). 
*   max files = 128 (can be changed - see the respective #define (MAXFILES)
*   a/the list of valid files will be generated during initialization.
*
*   micro SD card should be *class 10* !
*
*   - 'raw' files that go on the flash need to be stored in a folder called /SERFLASH
*   technically, they're not simply raw data; ie they *must* be created with wav2raw.c 
*
*   - TD: fix SPIFIFO for CS = 13
*   - TD: move eof to bytes
*   - TD: spi flash parsing -> char[]
*/

//#define REV1  // uncomment if using rev 1 boards

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <rotaryplus.h> 
#include <play_rawflash13.h> // change to <play_rawflash15.h> if using rev1 boards

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
  
    uint16_t    id;            // channel L/R
    uint16_t    file_wav;      // fileSelect
    uint16_t    state;         // channel state
    uint32_t    pos0;          // file start pos manual
    uint32_t    posX;          // end pos
    uint32_t    srt;           // start pos
    uint32_t    ctrl_res;      // start pos resolution (in bytes)
    uint32_t    ctrl_res_eof;  // eof resolution  (in ms) 
    float       _gain;         // volume 
    uint32_t    eof;           // end of file (in ms)
    uint16_t    swap;          // ping-pong file (1/2; 3/4)
    uint16_t    bank;          // bank: SD / Flash

} audioChannel;

struct audioChannel *audioChannels[CHANNELS];

/* ----------------------- channel misc ------------------- */

const uint16_t FADE_IN  = 3;         // fade in  (adjust to your liking)
const uint16_t FADE_OUT = 100;       // fade out (ditto)
const uint16_t FADE_IN_RAW  = 1;     // fade in  / flash
const uint16_t _FADE_F_CHANGE = 300; // fade out / file change 

uint32_t _FADE_TIMESTAMP_F_CHANGE = 0;

uint16_t FADE_LEFT, FADE_RIGHT, _EOF_L_OFF, _EOF_R_OFF;
uint32_t _LCLK_TIMESTAMP, _RCLK_TIMESTAMP, _EOF_L_TIMESTAMP, _EOF_R_TIMESTAMP; // trigger + E-o-F timestamps

uint16_t SPI_FLASH_STATUS = 0;

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

int16_t  _cv[numADC];
uint16_t ADC_cycle;
uint16_t _MIDPOINT = 0x0; 

/* encoders */ 
Rotary encoder[2] = {{ENC_L1, ENC_L2}, {ENC_R1, ENC_R2}}; 

/* ----------------------- timers + ISR stuff ------------------------ */

volatile uint16_t LCLK;   
volatile uint16_t RCLK;
uint32_t _TIMESTAMP_BUTTON;
const uint16_t DEBOUNCE = 250;

void FASTRUN CLK_ISR_L() 
{ 
  LCLK = true; 
}
void FASTRUN CLK_ISR_R() 
{ 
  RCLK = true; 
}

IntervalTimer UI_timer, ADC_timer;
volatile uint8_t UI  = false;
volatile uint8_t _ADC = false;

#define UI_RATE  10000  // UI update rate
#define ADC_RATE 250    // ADC sampling rate (*4)

void UItimerCallback()  
{ 
  UI = true;  
}
void ADCtimerCallback() 
{ 
  _ADC = true; 
}

/* ------------------------------------------------------ */

void setup() {
  
  //while (!Serial) {;}  
  analogReference(EXTERNAL);
  analogReadRes(ADC_RES);
  analogReadAveraging(16);   
  // clk inputs and switches -- need the pullups 
  pinMode(CLK_L, INPUT_PULLUP);
  pinMode(CLK_R, INPUT_PULLUP);
  pinMode(BUTTON_L, INPUT_PULLUP); 
  pinMode(BUTTON_R, INPUT_PULLUP); 
  // clk outputs
  pinMode(EOF_L, OUTPUT);
  pinMode(EOF_R, OUTPUT);  
  digitalWrite(EOF_L, LOW);
  digitalWrite(EOF_R, LOW);
  #ifndef REV1
      pinMode(CS_MEM, OUTPUT);    
      digitalWriteFast(CS_MEM, HIGH);
  #endif    
  
  // audio API, SD:
  AudioMemory(35);
  SPI.setMOSI(7);
  SPI.setSCK(14);
  
  if (!(SD.begin(CS_SD))) {
    
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
  delay(100);
  // set up TX --> atmega : 
  HWSERIAL.begin(BAUD);
  HWSERIAL.print('\n');
  delay(10);
  
  // zero volume while we generate the file list 
  mixL.gain(0, 0);
  mixL.gain(1, 0);
  mixL.gain(2, 0);
  mixL.gain(3, 0);
  mixR.gain(0, 0);
  mixR.gain(1, 0);
  mixR.gain(2, 0);
  mixR.gain(3, 0);
  
  //  get wav from SD : 
  generate_file_list();
  // and from spi flash -
  if (SPI_FLASH) SPI_FLASH_STATUS = spi_flash_init();
  // update spi flash ? 
  if (!digitalRead(BUTTON_R) && SPI_FLASH_STATUS) SPI_FLASH_STATUS = spi_flash(); 
  //  files on spi flash ? 
  if (SPI_FLASH_STATUS) SPI_FLASH_STATUS = extract();
  //  file names :
  if (SPI_FLASH_STATUS) generate_file_list_flash();
  
  // ADC + UI timers :
  ADC_timer.begin(ADCtimerCallback, ADC_RATE); 
  UI_timer.begin(UItimerCallback, UI_RATE);
  
  // allocate memory for L/R + init :
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
  //  calibrate mid point ?
  if (!digitalRead(BUTTON_L)) calibrate(); 
  else if (EEPROM.read(0x0)==0xFF) _MIDPOINT = readMIDpoint();
  else _MIDPOINT = pow(2,ADC_RES-1)-1;
  
  update_display(LEFT,  INIT_FILE);
  update_display(RIGHT, INIT_FILE);
 
  // set volume 
  mixL.gain(0, audioChannels[LEFT]->_gain);
  mixL.gain(1, audioChannels[LEFT]->_gain);
  mixL.gain(2, audioChannels[LEFT]->_gain);
  mixL.gain(3, audioChannels[LEFT]->_gain);
  mixR.gain(0, audioChannels[RIGHT]->_gain);
  mixR.gain(1, audioChannels[RIGHT]->_gain);
  mixR.gain(2, audioChannels[RIGHT]->_gain);
  mixR.gain(3, audioChannels[RIGHT]->_gain);
  //info();
  //print_wav_info();
}

/* main loop, wherein we mainly wait for the clock-flags */

void loop() 
{
  while(1) 
  {
     _loop();
  } 
}

/* ------------------------------------------------------ */



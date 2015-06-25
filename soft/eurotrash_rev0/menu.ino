/*
*
*  UI : display / encoders
*
*/

#define MAXFILES 128                 // we don't allow more than 128 files (for no particular reason); 
uint8_t FILECOUNT;
uint8_t RAW_FILECOUNT;
const uint8_t DISPLAY_LEN = 9;       // 8 (8.3) + 1 (active file indicator)
const uint8_t NAME_LEN = 13;         // 8.3
/* misc arrays */
char FILES[MAXFILES][NAME_LEN];              // file name, SD
uint32_t RAW_FILE_ADR[MAXFILES+0x1];   // file adr, SPI
char DISPLAYFILES[MAXFILES][NAME_LEN];       // display for SD
char RAW_DISPLAYFILES[MAXFILES][NAME_LEN];   // display for spi flash

uint32_t CTRL_RES[MAXFILES*CHANNELS];
uint32_t CTRL_RES_EOF[MAXFILES*CHANNELS];

enum {
   _SD, 
   _FLASH
};

float DEFAULT_GAIN = 0.6;            // adjust default volume [0.0 - 1.0]
uint8_t ENCODER_SWAP, DIR;           // alternate reading the encoders
const uint8_t CTRL_RESOLUTION = 100; // ctrl resolution (encoders), relative to file size; adjust to your liking (< 9999)
const float   CTRL_RESOLUTION_INV = 1.0f/(float)CTRL_RESOLUTION;
int16_t prev_encoderdata[]  = {-999, -999};

uint8_t MENU_PAGE[CHANNELS] = {0,0};
uint8_t filedisplay[CHANNELS];

// misc messages 
const char *_SAVE = "    save?";
const char *_OK   = "       OK";
const char *_FLASH_OK       = " FLASH OK";
const char *_FLASH_NOT_OK   = "    ERROR";
const char *_FCNT           = " FILES OK";
const char *_ALLGOOD        = "     A-OK";
const char *_DOT            = "         ";
const char *_SD_ERROR       = " SD EMPTY";
const char *_ERASE          = "... ERASE";
const char *_FLASHING       = " FLASHING";

/* menu pages */
enum { 
   FILESELECT,
   STARTPOS,
   ENDPOS,
   CALIBRATE,
   FLASH 
};

enum _button_states {
   READY,
   PRESSED,
   SHORT,
   HOLD,
   DONE 
};

enum {
  FLASH_OK,
  FLASH_NOT_OK,
  FCNT, 
  ALLGOOD,
  SD_ERROR,
  ERASE,
  FLASHING
};

uint32_t LASTBUTTON_EVENT =0 ;
uint8_t  SWAP_BUTTON = 0;
uint8_t  STATE_B[] = {0,0}, EVENT_B[] ={0,0};
const uint16_t LONGPRESSED = 800;
  
/* --------------------------------------------------- */

void left_encoder_ISR() {
  encoder[LEFT].process();
}

void right_encoder_ISR() {
  encoder[RIGHT].process();
}

/* --------------------------------------------------- */

void update_enc() {
  
  int16_t encoderdata;
  uint8_t _channel = ENCODER_SWAP;
  ENCODER_SWAP = ~_channel & 1u; // toggle L/R
   
  if (encoder[_channel].change()) { 
        encoderdata = encoder[_channel].pos();
        update_display(_channel, encoderdata);
  }
}  

void update_buttons() {
  
   if (!digitalReadFast(BUTTON_L) && !STATE_B[LEFT]  && (millis() - LASTBUTTON > DEBOUNCE))  { 
       
      STATE_B[LEFT]  = PRESSED;
      LASTBUTTON_EVENT = millis(); 
 
   }
   if (!digitalReadFast(BUTTON_R) && !STATE_B[RIGHT] && (millis() - LASTBUTTON > DEBOUNCE))  {
     
      STATE_B[RIGHT] = PRESSED;
      LASTBUTTON_EVENT = millis(); 
   }
}

void process_buttons() {
  
      
  uint8_t _button = ~SWAP_BUTTON & 1u;
  SWAP_BUTTON = _button;
      
  if (STATE_B[_button]) {
   
        uint8_t _b; 
        if (_button == LEFT) _b = digitalReadFast(BUTTON_L);
        else _b = digitalReadFast(BUTTON_R);
      
        if (_b && STATE_B[_button] == PRESSED)  EVENT_B[_button] = SHORT;
        else if (STATE_B[_button] == PRESSED && millis() - LASTBUTTON_EVENT > LONGPRESSED) EVENT_B[_button] = HOLD;
        else if (_b && EVENT_B[_button] == DONE)  STATE_B[_button] = READY;
      
        if (EVENT_B[_button] == SHORT) { 
          buttons(_button);
          STATE_B[_button] = EVENT_B[_button] = DONE;
          LASTBUTTON = millis(); 
        }
        else if (EVENT_B[_button] == HOLD) {
          // switch banks ?
          if (SPI_FLASH_STATUS+SPI_FLASH) { 
                uint16_t _bank = audioChannels[_button]->bank;
                uint16_t _xxx  = _bank*0x04 + CHANNELS*audioChannels[_button]->id;
                // fade out channel
                fade[_xxx]->fadeOut(FADE_OUT);
                _xxx++;
                fade[_xxx]->fadeOut(FADE_OUT);
                // update
                audioChannels[_button]->bank = ~_bank & 1u;
                uint16_t _file = 0x0;
                audioChannels[_button]->file_wav = _file; 
                MENU_PAGE[_button] = FILESELECT;
                update_display(_button, _file);
                
          }  
          EVENT_B[_button] = STATE_B[_button] = DONE; 
          LASTBUTTON = millis(); 
      }
   }
}
/* --------------------------------------------------------------- */

void buttons(uint8_t _channel) {
  
  uint8_t _ch = _channel;
  
  switch (MENU_PAGE[_ch]) {
    
  case FILESELECT: { // update file  
  
          uint8_t _file = audioChannels[_ch]->file_wav;
          if (filedisplay[_ch] != _file) update_channel(audioChannels[_ch]); // select new file
          else { // go to next page
              MENU_PAGE[_ch] = STARTPOS;
              int16_t start_pos = audioChannels[_ch]->pos0;
              encoder[_channel].setPos(start_pos);
              update_display(_ch, start_pos);
          }    
          break;
  }
  
  case STARTPOS: { // start pos
  
          MENU_PAGE[_ch] = ENDPOS;
          int16_t end_pos = audioChannels[_ch]->posX;
          encoder[_channel].setPos(end_pos);
          update_display(_ch, end_pos);
          break; 
  } 
  
  case ENDPOS: { // end pos
  
          MENU_PAGE[_ch] = FILESELECT;
          int8_t _file = audioChannels[_ch]->file_wav;
          encoder[_channel].setPos(_file);
          update_display(_ch, _file);
          break; 
  }     
  
  case CALIBRATE: {
          
          break;
        
   }
   
   case FLASH: {
          
          break;
        
   }
     
  default: break;  
 } 
}  

/* --------------------------------------------------------------- */

void update_channel(struct audioChannel* _ch) {
        
        uint8_t _id   = _ch->id;          // L or R ?
        uint8_t _file = filedisplay[_id]; // file #
        _ch->file_wav = _file;            // select file
        update_display(_id, _file);       // update menu
        _ch->_open = false;               // close prev files
        
        fade[_id*CHANNELS]->fadeOut(_FADE_F_CHANGE);
        fade[_id*CHANNELS+0x1]->fadeOut(_FADE_F_CHANGE);
        _FADE_TIMESTAMP_F_CHANGE = millis();

}  

/* --------------------------------------------------------------- */

void value_to_msg(char* _msg, int16_t _num) {
 
    char msg[] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0' };
     
    if (_num > 99)      sprintf(msg+6, "%d", _num); 
    else if (_num > 9)  sprintf(msg+7, "%d", _num); 
    else if (_num >= 0) sprintf(msg+8, "%d", _num); 
    
    memcpy(_msg, msg, DISPLAY_LEN+1);
}

/* --------------------------------------------------------------- */

void update_display(uint8_t _channel, uint16_t _newval) {
  
 char msg[DISPLAY_LEN+1];
 int16_t tmp = _newval;
 uint8_t _bank = audioChannels[_channel]->bank;
 char cmd = 0x02;      // this is the cmd byte / prefix for the Serial messages (ascii starting at 0x02).
 
 switch (MENU_PAGE[_channel]) {
   
     case FILESELECT: { // file
           uint8_t   max_f = (_bank == _SD) ? (FILECOUNT-1) : (RAW_FILECOUNT-1);
           if (tmp < 0)  {
                 tmp = max_f; 
                 encoder[_channel].setPos(max_f); 
                 DIR = false;
             }
           else if (tmp > max_f && DIR) {
                tmp = 0; encoder[_channel].setPos(0); 
             }
           else DIR = true;
           
           if (tmp > max_f) tmp = max_f;
           
           if (_bank == _FLASH) memcpy(msg, RAW_DISPLAYFILES[tmp], DISPLAY_LEN+1); 
           else memcpy(msg, DISPLAYFILES[tmp], DISPLAY_LEN+1);
           
           if (tmp == audioChannels[_channel]->file_wav) msg[0] = '\xb7';
           filedisplay[_channel] = tmp;  
           break;
     }
   
     case STARTPOS: {  
            if (tmp < 0) { tmp = 0; encoder[_channel].setPos(0x0); }
            else if (tmp > CTRL_RESOLUTION) { tmp = CTRL_RESOLUTION; encoder[_channel].setPos(CTRL_RESOLUTION);}
            audioChannels[_channel]->pos0 = tmp;
            //memcpy(msg, makedisplay(tmp).c_str(), DISPLAY_LEN+1);
            value_to_msg(msg, tmp);
            cmd +=0x02;
            break;
          
     }
     case ENDPOS: {
           if (tmp < 1)  { tmp = 1; encoder[_channel].setPos(0x1); }
           else if (tmp > CTRL_RESOLUTION) { tmp = CTRL_RESOLUTION; encoder[_channel].setPos(CTRL_RESOLUTION);}
           audioChannels[_channel]->posX = tmp;
           //memcpy(msg, makedisplay(tmp).c_str(), DISPLAY_LEN+1);
           value_to_msg(msg, tmp);
           cmd +=0x04;
           break;
     }  
     
     case CALIBRATE: {
           //if (!_channel) memcpy(msg, makedisplay(tmp).c_str(), DISPLAY_LEN+1); // left = display ADC
           if (!_channel) value_to_msg(msg, tmp);
           else if (_channel && _newval > 0) memcpy(msg, _SAVE, DISPLAY_LEN);
           else memcpy(msg, _OK, DISPLAY_LEN);
           break;
        
     }
     
     case FLASH: {
          
           if (!_channel) {           
               switch (_newval) {                     
                       case FLASH_OK:     { memcpy(msg, _FLASH_OK, DISPLAY_LEN);     break; }
                       case FLASH_NOT_OK: { memcpy(msg, _FLASH_NOT_OK, DISPLAY_LEN); break; }
                       case ALLGOOD:      { memcpy(msg, _ALLGOOD,  DISPLAY_LEN);     break; }
                       case SD_ERROR:     { memcpy(msg, _SD_ERROR, DISPLAY_LEN);     break; }
                       case ERASE:        { memcpy(msg, _ERASE,  DISPLAY_LEN);       break; }
                       case FLASHING:     { memcpy(msg, _FLASHING, DISPLAY_LEN);     break; }
                       default: break;
               }            
               break;
          }
          else  { 
              switch (_newval) {       
                      case FCNT:           { memcpy(msg, _FCNT, DISPLAY_LEN);   break; }
                      case ALLGOOD:        { memcpy(msg, _DOT,  DISPLAY_LEN);   break; }
                      default: break;
              }
              break;
          }
    }
    default: break;  
 }
 
 // send to atmega 
 cmd += _channel;
 HWSERIAL.print(cmd);
 HWSERIAL.print(msg);
}

/* --------------------------------------------------------------- */

void _UI() {
  
       update_enc();
       update_buttons();
       process_buttons();
       UI = false;
     }
     
/* --------------------------------------------------------------- */

void _adc() {
 
       _ADC = false;
       ADC_cycle++;
       if (ADC_cycle >= numADC)  ADC_cycle = 0; 
       CV[ADC_cycle] = analogRead(ADC_cycle+0x10);
       update_eof(ADC_cycle);
}  


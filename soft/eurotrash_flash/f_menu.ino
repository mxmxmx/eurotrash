/*
*
*  UI : display / encoders
*
*/

const uint16_t MAXFILES = 128;        // we don't allow more than 128 files (for no particular reason); 
uint16_t FILECOUNT;
uint16_t RAW_FILECOUNT;
const uint16_t DISPLAY_LEN = 9;       // 8 (8.3) + 1 (active file indicator)
const uint16_t NAME_LEN = 13;         // 8.3
/* misc arrays */
char FILES[MAXFILES*CHANNELS][NAME_LEN];         // file names
//uint32_t RAW_FILE_ADR[MAXFILES+0x1];           // file adr, SPI
//uint16_t SD_FILE_INDEX[MAXFILES];              // SD file indices
char DISPLAYFILES[MAXFILES*CHANNELS][NAME_LEN];  // display names
//char RAW_DISPLAYFILES[MAXFILES][NAME_LEN];  

uint32_t CTRL_RES[MAXFILES*CHANNELS];
uint32_t CTRL_RES_EOF[MAXFILES*CHANNELS];

enum { // bank
   _SD, 
   _FLASH
};

enum { // channel state
  
  _STOP,
  _PLAY,
  _PAUSE, 
  _RETRIG
};

const uint16_t _WAIT = 100;          // channel wait state (ms)


float DEFAULT_GAIN = 0.6;             // adjust default volume [0.0 - 1.0]
uint16_t ENCODER_SWAP, DIR;           // alternate reading the encoders
const uint16_t CTRL_RESOLUTION = 100; // ctrl resolution (encoders), relative to file size; adjust to your liking (< 9999)
const float   CTRL_RESOLUTION_INV = 1.0f/(float)CTRL_RESOLUTION;
int16_t prev_encoderdata[]  = {-999, -999};

uint16_t MENU_PAGE[CHANNELS] = {0,0};
uint16_t filedisplay[CHANNELS];

// misc messages 
const char *_SAVE = "    save?";
const char *_OK   = "       OK";
const char *_FLASH_OK       = " FLASH OK";
const char *_FLASH_NOT_OK   = "    ERROR";
const char *_FILES_OK       = " FILES OK";
const char *_ALLGOOD        = "     A-OK";
const char *_DOT            = "         ";
const char *_SD_ERROR       = " SD ERROR";
const char *_ERASE          = "... ERASE";
const char *_FLASHING       = " FLASHING";

/* menu pages */
enum { 
   FILESELECT,
   STARTPOS,
   ENDPOS,
   CALIBRATE,
   ERASE_FLASH,
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
  FILES_OK, 
  ALLGOOD,
  SD_ERROR,
  ERASE,
  FLASHING
};

uint32_t _TIMESTAMP_BUTTON_EVENT =0 ;
uint16_t  SWAP_BUTTON = 0;
uint16_t  STATE_B[] = {0,0}, EVENT_B[] ={0,0};
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
  uint16_t _channel = ENCODER_SWAP;
  ENCODER_SWAP = ~_channel & 1u; // toggle L/R
   
  if (encoder[_channel].change()) { 
        encoderdata = encoder[_channel].pos();
        update_display(_channel, encoderdata);
  }
}  

void update_buttons() {
  
   if (!digitalReadFast(BUTTON_L) && !STATE_B[LEFT]  && (millis() - _TIMESTAMP_BUTTON > DEBOUNCE))  { 
       
      STATE_B[LEFT]  = PRESSED;
      _TIMESTAMP_BUTTON_EVENT = millis(); 
 
   }
   if (!digitalReadFast(BUTTON_R) && !STATE_B[RIGHT] && (millis() - _TIMESTAMP_BUTTON > DEBOUNCE))  {
     
      STATE_B[RIGHT] = PRESSED;
      _TIMESTAMP_BUTTON_EVENT = millis(); 
   }
}

void process_buttons() {
  
      
  uint16_t _button = ~SWAP_BUTTON & 1u;
  SWAP_BUTTON = _button;
      
  if (STATE_B[_button]) {
   
        uint16_t _b; 
        if (_button == LEFT) _b = digitalReadFast(BUTTON_L);
        else _b = digitalReadFast(BUTTON_R);
      
        if (_b && STATE_B[_button] == PRESSED)  EVENT_B[_button] = SHORT;
        else if (STATE_B[_button] == PRESSED && millis() - _TIMESTAMP_BUTTON_EVENT > LONGPRESSED) EVENT_B[_button] = HOLD;
        else if (_b && EVENT_B[_button] == DONE)  STATE_B[_button] = READY;
      
        if (EVENT_B[_button] == SHORT) { 
          buttons(_button);
          STATE_B[_button] = EVENT_B[_button] = DONE;
          _TIMESTAMP_BUTTON = millis(); 
        }
        else if (EVENT_B[_button] == HOLD) {
          // switch banks ?
          if (SPI_FLASH_STATUS+SPI_FLASH) { 
            
                uint16_t _bank = audioChannels[_button]->bank;
                uint16_t _voice_num = _bank*0x04 + CHANNELS*audioChannels[_button]->id;
                // fade out channel
                fade[_voice_num]->fadeOut(FADE_OUT);
                _voice_num++;
                fade[_voice_num]->fadeOut(FADE_OUT);
                // update
                audioChannels[_button]->bank = ~_bank & 1u;
                uint16_t _file = 0x0;
                audioChannels[_button]->file_wav = _file; 
                MENU_PAGE[_button] = FILESELECT;
                update_display(_button, _file);
                
                audioChannels[_button]->state = _STOP; // pause channel
                _FADE_TIMESTAMP_F_CHANGE = millis();
                
                if (!audioChannels[_button]->id) {
                   _EOF_L_OFF = false;
                   FADE_LEFT = true;
                } 
                else {
                  _EOF_R_OFF = false;
                  FADE_RIGHT = true;
                }
          }  
          EVENT_B[_button] = STATE_B[_button] = DONE; 
          _TIMESTAMP_BUTTON = millis(); 
      }
   }
}
/* --------------------------------------------------------------- */

void buttons(uint16_t _channel) {
  
  uint16_t _ch = _channel;
  
  switch (MENU_PAGE[_ch]) {
    
  case FILESELECT: { // update file  
  
          uint16_t _file = audioChannels[_ch]->file_wav;
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
          int16_t _file = audioChannels[_ch]->file_wav;
          encoder[_channel].setPos(_file);
          update_display(_ch, _file);
          break; 
  }     
  
  case CALIBRATE: {
          
          break;
        
   }
   
   case ERASE_FLASH: {
          
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
        
       uint16_t _id, _bank, _file;
       
         _id   = _ch->id;          // L or R ?
         _bank = _ch->bank;
         _file = filedisplay[_id]; // file #
        
        _ch->file_wav = _file;             // select file
        update_display(_id, _file);        // update menu
        _ch->state = _STOP;                // pause channel
        
        if (!_id) {
             
             _EOF_L_OFF = false;
             FADE_LEFT = true;
        } 
        else {
          
             _EOF_R_OFF = false;
             FADE_RIGHT = true;
        }
        fade[_id*CHANNELS + _bank*0x4]->fadeOut(_FADE_F_CHANGE);
        fade[_id*CHANNELS + _bank*0x4 + 0x1]->fadeOut(_FADE_F_CHANGE);
        _FADE_TIMESTAMP_F_CHANGE = millis();
}  

/* --------------------------------------------------------------- */

void value_to_msg(char* _msg, int16_t _num) {
 
    char msg[] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0' };
     
    if (_num > 999)     sprintf(msg+5, "%d", _num);  
    else if (_num > 99) sprintf(msg+6, "%d", _num); 
    else if (_num > 9)  sprintf(msg+7, "%d", _num); 
    else if (_num >= 0) sprintf(msg+8, "%d", _num); 
    
    memcpy(_msg, msg, DISPLAY_LEN+1);
}

/* --------------------------------------------------------------- */

void update_display(uint8_t _channel, uint16_t _newval) {
  
 char msg[DISPLAY_LEN+1];
 int16_t tmp = _newval;
 uint16_t _bank = audioChannels[_channel]->bank;
 char cmd = 0x02;      // this is the cmd byte / prefix for the Serial messages (ascii starting at 0x02).
 
 switch (MENU_PAGE[_channel]) {
   
     case FILESELECT: { // file
     
           uint16_t max_f = (_bank == _SD) ? (FILECOUNT-1) : (RAW_FILECOUNT-1);
           if (tmp < 0)  {
                 tmp = max_f; 
                 encoder[_channel].setPos(max_f); 
                 DIR = false;
             }
           else if (tmp > max_f && DIR) {
                tmp = 0; encoder[_channel].setPos(0); 
             }
           else DIR = true;
           
           tmp = (tmp > max_f) ? max_f : tmp;
           memcpy(msg, DISPLAYFILES[tmp + _bank*MAXFILES], DISPLAY_LEN+1);
           
           // decorate the selected file: 
           if (tmp == audioChannels[_channel]->file_wav) msg[0] = '\xb7';
           filedisplay[_channel] = tmp;  
           break;
     }
   
     case STARTPOS: {  
            if (tmp < 0) { tmp = 0; encoder[_channel].setPos(0x0); }
            else if (tmp > CTRL_RESOLUTION) { tmp = CTRL_RESOLUTION; encoder[_channel].setPos(CTRL_RESOLUTION);}
            audioChannels[_channel]->pos0 = tmp;
            value_to_msg(msg, tmp);
            cmd +=0x02;
            break;
          
     }
     case ENDPOS: {
           if (tmp < 1)  { tmp = 1; encoder[_channel].setPos(0x1); }
           else if (tmp > CTRL_RESOLUTION) { tmp = CTRL_RESOLUTION; encoder[_channel].setPos(CTRL_RESOLUTION);}
           audioChannels[_channel]->posX = tmp;
           value_to_msg(msg, tmp);
           cmd +=0x04;
           break;
     }  
     
     case CALIBRATE: {
           
           if (!_channel) value_to_msg(msg, tmp);
           else if (_channel && _newval > 0) memcpy(msg, _SAVE, DISPLAY_LEN);
           else memcpy(msg, _OK, DISPLAY_LEN);
           break;
        
     }
     
      case ERASE_FLASH: {
           
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
                       case FILES_OK:     { memcpy(msg, _FILES_OK, DISPLAY_LEN);     break; }
                       case ALLGOOD:      { memcpy(msg, _ALLGOOD,  DISPLAY_LEN);     break; }
                       case SD_ERROR:     { memcpy(msg, _SD_ERROR, DISPLAY_LEN);     break; }
                       case ERASE:        { memcpy(msg, _ERASE,  DISPLAY_LEN);       break; }
                       case FLASHING:     { memcpy(msg, _FLASHING, DISPLAY_LEN);     break; }
                       default: break;
               }            
               break;
          }
          else  { 
                if (_newval) value_to_msg(msg, _newval);
                else memcpy(msg, _DOT, DISPLAY_LEN); 
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

void _UI() 
{
  update_enc();
  update_buttons();
  process_buttons();
  UI = false;
}
     
/* --------------------------------------------------------------- */

void _adc() 
{
  uint16_t cnt = _ADC_cycle;
  
  cnt = (cnt++ >= 0x3) ? 0x0 : cnt;  // reset counter
  _cv[cnt] = (_MIDPOINT - analogRead(cnt+0x10)) >> 0x5;  
  update_eof(cnt);
  _ADC_cycle = cnt;
  _ADC = false; 
}

/* --------------------------------------------------------------- */

void calibrate() {
  
  /*  calibrate mid point */
      float average = 0.0f;
      uint8_t save = false;
      _MIDPOINT = 0;
      MENU_PAGE[LEFT]  = CALIBRATE;
      MENU_PAGE[RIGHT] = CALIBRATE;
      update_display(LEFT, _MIDPOINT);
      delay(1000);
      for (int i = 0; i < 200; i++) {
   
           average +=  analogRead(CV1);
           delay(2);
           average +=  analogRead(CV2);
           delay(2);
           average +=  analogRead(CV3);
           delay(2);
           average +=  analogRead(CV4);
           delay(2);
      }
      
      _MIDPOINT = average / 800.0f;
      update_display(LEFT,  _MIDPOINT);
      Serial.println(_MIDPOINT);
      delay(500);
      update_display(RIGHT, _MIDPOINT);
      // do we want to save the value?
      while(digitalRead(BUTTON_L)) {
        
           if (!digitalRead(BUTTON_R) && !save) { 
                 save = true; 
                 writeMIDpoint(_MIDPOINT);
                 update_display(RIGHT, 0x0);
            }
        
      }
      delay(1000);
      MENU_PAGE[LEFT]  = FILESELECT; 
      MENU_PAGE[RIGHT] = FILESELECT; 
      _TIMESTAMP_BUTTON = millis(); 
} 

/* --------------------------------------------------------------- */

void writeMIDpoint(uint16_t _val) 
{   
  uint8_t byte0, byte1, adr = 0;
       
  byte0 = _val >> 0x8;
  byte1 = _val;
  EEPROM.write(adr, 0xFF);
  adr++;
  EEPROM.write(adr, byte0);
  adr++;
  EEPROM.write(adr, byte1);
}  

/* --------------------------------------------------------------- */

uint16_t readMIDpoint()
{  
  uint8_t byte0, byte1, adr = 0x1;
  
  byte0 = EEPROM.read(adr);
  adr++;
  byte1 = EEPROM.read(adr);
  return  (uint16_t)(byte0 << 8) + byte1;
}  

/* =============================================== */

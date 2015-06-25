/*
*       
* some things to deal with the wav files. mainly opening + swapping the audio objects
*
*/

void init_channels(uint8_t f) {
  
  uint8_t _file = f;
  for (int i = 0; i < CHANNELS; i ++) {
        
        audioChannels[i]->id = i;
        audioChannels[i]->file_wav = _file;
        audioChannels[i]->_open = 0;
        audioChannels[i]->pos0 = 0;
        audioChannels[i]->posX = CTRL_RESOLUTION;
        audioChannels[i]->srt = 0;
        audioChannels[i]->ctrl_res = CTRL_RES[_file];
        audioChannels[i]->ctrl_res_eof = CTRL_RES_EOF[_file];
        audioChannels[i]->eof = CTRL_RESOLUTION * CTRL_RES_EOF[_file];
        audioChannels[i]->_gain = DEFAULT_GAIN;  
        audioChannels[i]->swap = false;
        audioChannels[i]->bank = false;
  } 
}  

/* ====================check clocks================ */

void leftright() {
  
 if (LCLK) {  // clock?
  
       if (audioChannels[LEFT]->_open)  _play(audioChannels[LEFT]); 
       LCLK = false;
       FADE_LEFT = false;
       _LCLK_TIMESTAMP = millis();
  } 
  if (RCLK) { // clock?
 
       if (audioChannels[RIGHT]->_open) _play(audioChannels[RIGHT]);
       RCLK = false;
       FADE_RIGHT = false;
       _RCLK_TIMESTAMP = millis();
   } 
}

/* =============================================== */

void _play(struct audioChannel* _channel) {
  
      uint16_t _swap, _bank, _numVoice, _id;
      int32_t _startPos;
      
      _swap   = _channel->swap;   
      _bank   = _channel->bank;     
      _id     = _channel->id; 
      
      _numVoice = _swap + _id*CHANNELS;               // select audio object # 1,2 (LEFT) or 3,4 (RIGHT) 
      
      _startPos = (HALFSCALE - CV[0x3-_id]) >> 0x5;   // CV
      _startPos += _channel->pos0;                    // manual offset  
      _startPos = _startPos < 0 ?  0 : _startPos;     // limit  
      _startPos = _startPos < CTRL_RESOLUTION ? _startPos : (CTRL_RESOLUTION - 0x1);    
      _channel->srt = _startPos;                      // remember start pos        
      _startPos *= _channel->ctrl_res;                // scale => bytes / frames
     
       if (_bank) {
            _channel->srt = 0x00;                     // actually, don't remember start pos (this doesn't work properly for short files / integers)
            uint16_t _file   = _channel->file_wav;
            fade[_numVoice+0x4]->fadeIn(FADE_IN_RAW);
            const unsigned int f_adr = RAW_FILE_ADR[_file]; 
            raw[_numVoice]->seek(f_adr, _startPos);
       }
       else {
             fade[_numVoice]->fadeIn(FADE_IN);
             wav[_numVoice]->seek(_startPos>>9);    
       }   
       /*  swap file and fade out previous file: */  
        _swap = ~_swap & 1u;
        fade[_swap + _id*CHANNELS + _bank*0x4]->fadeOut(FADE_OUT); // ?
        _channel->swap = _swap;
        !_id ? PAUSE_FILE_L = true : PAUSE_FILE_R = true;
}
 
/* =============================================== */

void eof_left() {
    
   // uint16_t _swap = ~audioChannels[LEFT]->swap & 1u;
   // uint32_t _pos  = _bank ? raw[_swap]->position() : wav[_swap]->positionBytes();
  
   if (millis() - _LCLK_TIMESTAMP > audioChannels[LEFT]->eof) {
  
        uint8_t  _bank = audioChannels[LEFT]->bank;
        uint8_t  _swap = ~audioChannels[LEFT]->swap & 1u;
      
        digitalWriteFast(EOF_L, HIGH); 
        
        fade[_swap+_bank*0x4]->fadeOut(FADE_OUT); 
        _EOF_L_TIMESTAMP = millis();
        EOF_L_OFF = FADE_LEFT = true;
     }  
}

void eof_right() {
  
    // uint16_t _swap = ~audioChannels[RIGHT]->swap & 1u;
    // uint32_t _pos  = _bank ? raw[_swap]->position() : wav[_swap]->positionBytes();
  
    if (millis() - _RCLK_TIMESTAMP > audioChannels[RIGHT]->eof) {
       
        uint8_t  _bank = audioChannels[RIGHT]->bank;
        uint8_t  _swap = (~audioChannels[RIGHT]->swap & 1u) + CHANNELS; 
          
        digitalWriteFast(EOF_R, HIGH);  
            
        fade[_swap+_bank*0x4]->fadeOut(FADE_OUT);      
        _EOF_R_TIMESTAMP = millis();
        EOF_R_OFF = FADE_RIGHT = true;
     } 
}

/* =============================================== */

void _pause_active_L() { // pause voice that's playing

    if (millis() - _EOF_L_TIMESTAMP > FADE_OUT) { 
  
        digitalWriteFast(EOF_L, LOW);  
        uint16_t _swap = ~audioChannels[LEFT]->swap & 1u;
        wav[_swap]->pause(); 
        EOF_L_OFF = false; 
     }  
}

void _pause_active_R() {
  
     if (millis() - _EOF_R_TIMESTAMP > FADE_OUT) { 
  
        digitalWriteFast(EOF_R, LOW);  
        uint16_t _swap = (~audioChannels[RIGHT]->swap & 1u) + CHANNELS;
        wav[_swap]->pause();  
        EOF_R_OFF = false; 
     }  
}

/* =============================================== */

void _pause_inactive_L() { // pause voice that's no longer playing

    if (millis() - _LCLK_TIMESTAMP > FADE_OUT) { 
   
        uint16_t _swap = audioChannels[LEFT]->swap & 1u;
        wav[_swap]->pause(); 
        PAUSE_FILE_L = false; 
     }   
}

void _pause_inactive_R() { 
 
     if (millis() - _RCLK_TIMESTAMP > FADE_OUT) { 
   
        uint16_t _swap = (audioChannels[RIGHT]->swap & 1u) + CHANNELS;
        wav[_swap]->pause();  
        PAUSE_FILE_R = false; 
     } 
}

/* =============================================== */

void _open_next(struct audioChannel* _channel) {
  
     if (millis() - _FADE_TIMESTAMP_F_CHANGE > _FADE_F_CHANGE) {
      
         uint16_t  _id, _file;
     
         _file = _channel->file_wav;
         _id   = _channel->id*CHANNELS;
         //  update channel data: 
         _channel->ctrl_res = CTRL_RES[_file];
         _channel->ctrl_res_eof = CTRL_RES_EOF[_file]; 
      
         // close files : 
         wav[_id]->close();     
         wav[_id+0x1]->close(); 
         // open new files :  
         const char *thisfile = FILES[_file];
         wav[_id]->open(thisfile);
         wav[_id+0x1]->open(thisfile);
         _channel->swap  = 0x0;   // reset
         _channel->_open = 0x1;   // play
    }
    
    else if (_channel->bank) {  // SPI flash 
      
         uint16_t _id, _file;
    
         _file = _channel->file_wav;
         _id   = _channel->id*CHANNELS;  
         // update channel data: 
         _channel->ctrl_res = CTRL_RES[_file + MAXFILES];
         _channel->ctrl_res_eof = CTRL_RES_EOF[_file + MAXFILES]; 
         _channel->_open = 0x1; 
    }
}

/* =============================================== */

void generate_file_list() {  // to do - sort alphabetically?
  
  uint8_t len;
  uint32_t file_len, file_len_ms;
  char tmp[DISPLAY_LEN];
  File thisfile;
  root = SD.open("/");
  
  thisfile = root.openNextFile(O_RDONLY);  
  while (thisfile && FILECOUNT < MAXFILES) {
              char* _name = thisfile.name(); 
              // wav files ?  
              len = strlen(_name) - 4; 
              if  (!strcmp(&_name[len-2], "~1.WAV")) delay(2); // skip crap
              else if  (_name[0] == '_') delay(2);             // skip crap
              else if (!strcmp(&_name[len], ".WAV")) {
    
                      memcpy(FILES[FILECOUNT], _name, NAME_LEN);
                      /* this is annoying */
                      wav1.play(_name);
                      delay(15);
                      file_len = (float)wav1.lengthBytes() * 0.9f;
             
                      CTRL_RES[FILECOUNT]  = file_len * CTRL_RESOLUTION_INV;       // ctrl resolution pos0/bytes
                      file_len_ms = wav1.lengthMillis();
                      CTRL_RES_EOF[FILECOUNT] = file_len_ms * CTRL_RESOLUTION_INV; // ctrl resolution posX/bytes
                      wav1.stop();
                      /* for the display, get rid of .wav extension + right justify */
                      int8_t justify = DISPLAY_LEN - len;
                      if (justify < 0) justify = 0; 
                      for (int i = justify; i < DISPLAY_LEN; i++) {  
                          tmp[i] = _name[i-justify];
                          if (tmp[i] >= 'A' && tmp[i]  <= 'Z' ) tmp[i] = tmp[i] + 'a' - 'A';
                      } 
                      while (justify) {
                          justify--;
                          tmp[justify] = ' '; 
                      }
                      memcpy(DISPLAYFILES[FILECOUNT], tmp, sizeof(tmp));
                      FILECOUNT++;
              }    
             thisfile.close();
             thisfile = root.openNextFile(O_RDONLY);
   }   
  root.rewindDirectory(); 
  root.close();
}
  
/* =============================================== */

void update_eof(uint8_t _channel) {
  
   /* update EOF */
   if (_channel < CHANNELS) { 
     
       int32_t _srt, _end; 
       _end = (HALFSCALE - CV[_channel])>>0x5;               
       _end += audioChannels[_channel]->posX;               
       _end = _end < 0 ? 0 : _end;
       _end = _end < CTRL_RESOLUTION ? _end : CTRL_RESOLUTION - 0x1;
       
       _srt =  audioChannels[_channel]->srt;          
       _srt = (CTRL_RESOLUTION - _srt) * audioChannels[_channel]->ctrl_res_eof ; // = effective length in ms  
       _end = _srt * CTRL_RESOLUTION_INV * _end;
       audioChannels[_channel]->eof = _end > 0 ? _end : 0x01;
    }
}  
/* =============================================== */

void calibrate() {
  
  /*  calibrate mid point */
      float average = 0.0f;
      uint8_t save = false;
      HALFSCALE = 0;
      MENU_PAGE[LEFT]  = CALIBRATE;
      MENU_PAGE[RIGHT] = CALIBRATE;
      update_display(LEFT, HALFSCALE);
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
      
      HALFSCALE = average / 800.0f;
      update_display(LEFT,  HALFSCALE);
      delay(500);
      update_display(RIGHT, HALFSCALE);
      // do we want to save the value?
      while(digitalRead(BUTTON_L)) {
        
           if (!digitalRead(BUTTON_R) && !save) { 
                 save = true; 
                 writeMIDpoint(HALFSCALE);
                 update_display(RIGHT, 0x0);
            }
        
      }
      delay(1000);
      MENU_PAGE[LEFT]  = FILESELECT; 
      MENU_PAGE[RIGHT] = FILESELECT; 
      LASTBUTTON = millis(); 
} 

/* =============================================== */

void writeMIDpoint(uint16_t _val) {
   
  uint8_t byte0, byte1, adr = 0;
       
       byte0 = _val >> 8;
       byte1 = _val;
       EEPROM.write(adr, 0xFF);
       adr++;
       EEPROM.write(adr, byte0);
       adr++;
       EEPROM.write(adr, byte1);
}  

/* =============================================== */

uint16_t readMIDpoint() {
  
       uint8_t byte0, byte1, adr = 0x1;
       byte0 = EEPROM.read(adr);
       adr++;
       byte1 = EEPROM.read(adr);
       
       return  (uint16_t)(byte0 << 8) + byte1;
}  


/*
*       
* some things to deal with the wav files. mainly opening + swapping the audio objects
*
*/

void init_channels(uint8_t f) {
  
  uint16_t _file = f;
  for (int i = 0; i < CHANNELS; i ++) {
        
        audioChannels[i]->id = i;
        audioChannels[i]->file_wav = _file;
        audioChannels[i]->state = _STOP;
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
 
       if (audioChannels[LEFT]->state)  _play(audioChannels[LEFT]); 
       LCLK = FADE_LEFT = false;
       _LCLK_TIMESTAMP = millis();
  } 
  
  if (RCLK) { // clock?
 
       if (audioChannels[RIGHT]->state) _play(audioChannels[RIGHT]);
       RCLK = FADE_RIGHT = false;
       _RCLK_TIMESTAMP = millis();
   } 
}

/* =============================================== */

void _play(struct audioChannel* _channel) {
  
      uint16_t _swap, _bank, _numVoice, _id, _state;
      int32_t _startPos;
      
      _swap   = _channel->swap;   
      _state  = _channel->state;
      _bank   = _channel->bank;     
      _id     = _channel->id; 
      
      _numVoice = _swap + _id*CHANNELS; // select audio object # 1,2 (LEFT) or 3,4 (RIGHT) 
      
      _startPos = _cv[0x3-_id];                       // CV
      _startPos += _channel->pos0;                    // manual offset  
      _startPos = _startPos < 0 ?  0 : _startPos;     // limit  
      _startPos = _startPos < CTRL_RESOLUTION ? _startPos : (CTRL_RESOLUTION - 0x1);    
      _channel->srt = _startPos;                      // remember start pos        
      _startPos *= _channel->ctrl_res;                // scale => bytes / frames
     
       if (_bank) { // spi flash
            _channel->srt = 0x00; // hack
            uint16_t _file   = _channel->file_wav;
            fade[_numVoice+0x4]->fadeIn(FADE_IN_RAW);
            const unsigned int f_adr = RAW_FILE_ADR[_file]; 
            raw[_numVoice]->seek(f_adr, _startPos);
       }
       else { // SD
             fade[_numVoice]->fadeIn(FADE_IN);
             wav[_numVoice]->seek(_startPos>>9);    
       }   
       // swap file and fade out previous file :
        _swap = ~_swap & 1u;
        fade[_swap + _id*CHANNELS + _bank*0x4]->fadeOut(FADE_OUT); 
        _channel->swap = _swap;     
        _channel->state = (_state == _PLAY) ? _PAUSE : _RETRIG; 
}
 
/* =============================================== */

void eof_left() {

   if (millis() - _LCLK_TIMESTAMP > audioChannels[LEFT]->eof) {
  
        uint16_t  _bank = audioChannels[LEFT]->bank;
        uint16_t  _swap = ~audioChannels[LEFT]->swap & 1u;
      
        digitalWriteFast(EOF_L, HIGH); 
        
        fade[_swap+_bank*0x4]->fadeOut(FADE_OUT); 
        _EOF_L_TIMESTAMP = millis();
        _EOF_L_OFF = FADE_LEFT = true;
     }  
}

void eof_right() {
  
    if (millis() - _RCLK_TIMESTAMP > audioChannels[RIGHT]->eof) {
       
        uint16_t  _bank = audioChannels[RIGHT]->bank;
        uint16_t  _swap = (~audioChannels[RIGHT]->swap & 1u) + CHANNELS; 
          
        digitalWriteFast(EOF_R, HIGH);  
            
        fade[_swap+_bank*0x4]->fadeOut(FADE_OUT);      
        _EOF_R_TIMESTAMP = millis();
        _EOF_R_OFF = FADE_RIGHT = true;
     } 
}

/* =============================================== */

void _PAUSE_EOF_L() { // pause file + EOF trig 

    if (millis() - _EOF_L_TIMESTAMP > FADE_OUT) { 
  
        uint16_t _state = audioChannels[LEFT]->state;
      
        if (_state == _PAUSE) { // pause file? 
          
              uint16_t _swap  = ~audioChannels[LEFT]->swap & 1u;
              wav[_swap]->pause(); 
              audioChannels[LEFT]->state = _PLAY; // reset
        }
        else if (millis() - _LCLK_TIMESTAMP > (audioChannels[LEFT]->eof + _WAIT)) audioChannels[LEFT]->state = _PLAY; // resume
       
         digitalWriteFast(EOF_L, LOW);  
        _EOF_L_OFF = false;
     }  
}

void _PAUSE_EOF_R() {
  
     if (millis() - _EOF_R_TIMESTAMP > FADE_OUT) { 
        uint16_t _state = audioChannels[RIGHT]->state;
 
        if (_state == _PAUSE) {   // pause file? 
           
              uint16_t _swap = (~audioChannels[RIGHT]->swap & 1u) + CHANNELS; 
              wav[_swap]->pause(); 
              audioChannels[RIGHT]->state = _PLAY; // reset 
        }
        else if (millis() - _RCLK_TIMESTAMP > (audioChannels[RIGHT]->eof + _WAIT)) audioChannels[RIGHT]->state = _PLAY; // resume
       
        digitalWriteFast(EOF_R, LOW);  
        _EOF_R_OFF = false; 
     }  
}

/* =============================================== */

void _open_new(struct audioChannel* _channel) {
  
     if (millis() - _FADE_TIMESTAMP_F_CHANGE > _FADE_F_CHANGE) {
      
         uint16_t  _id, _file;
         
         _id   = _channel->id*CHANNELS;
         _file = _channel->file_wav;
         const char *_file_name = FILES[_file];
         
         // close files : 
         wav[_id]->close();     
         wav[_id+0x1]->close(); 
         // open new files : 
         wav[_id]->open(_file_name);
         wav[_id+0x1]->open(_file_name);
         
         //  update channel data: 
         _channel->ctrl_res = CTRL_RES[_file];
         _channel->ctrl_res_eof = CTRL_RES_EOF[_file]; 
         _channel->swap  = 0x0;     // reset
         _channel->state = _PLAY;  
    }
    
    else if (_channel->bank) {  // SPI flash 
      
         uint16_t _file = _channel->file_wav;
         // update channel data: 
         _channel->ctrl_res = CTRL_RES[_file + MAXFILES];
         _channel->ctrl_res_eof = CTRL_RES_EOF[_file + MAXFILES]; 
         _channel->swap  = 0x0;     // reset
         _channel->state = _PLAY; 
    }
}

/* =============================================== */

void generate_file_list() {  // to do - sort alphabetically?
  
  uint16_t len;
  uint32_t file_len, file_len_ms;
  char tmp[DISPLAY_LEN];
  File thisfile;
  root = SD.open("/");
  thisfile = root.openNextFile();

  while (thisfile && FILECOUNT < MAXFILES) {
    
              char* _name = thisfile.name();           
              // wav files ?  
              len = strlen(_name) - 4; 
 
              if  (!strcmp(&_name[len-2], "~1.WAV")) delay(2); // skip crap
              else if  (_name[0] == '_') delay(2);             // skip crap
              else if (!strcmp(&_name[len], ".WAV")) {
                
                      memcpy(FILES[FILECOUNT], _name, NAME_LEN);
                      //uint16_t _index = SD.returnFileIndex(_name);
                      //SD_FILE_INDEX[FILECOUNT] = _index;
                      wav1.play(_name);
                      delay(10); // delay until update();
                      file_len = (float)wav1.lengthBytes() * 0.9f;
             
                      CTRL_RES[FILECOUNT]  = file_len * CTRL_RESOLUTION_INV;       // ctrl resolution pos0/bytes
                      file_len_ms = wav1.lengthMillis();
                      CTRL_RES_EOF[FILECOUNT] = file_len_ms * CTRL_RESOLUTION_INV; // ctrl resolution posX/bytes
                      wav1.stop();
                      // for the display, get rid of .wav extension + right justify 
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
               thisfile = root.openNextFile(); 
  }   
  root.rewindDirectory(); 
  root.close();
}
  
/* =============================================== */

void update_eof(uint8_t _channel) {
  
   /* update EOF */
   if (_channel < CHANNELS) { 
     
       int32_t _srt, _end; 
       _end = _cv[_channel];               
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


///////////////////////////////////////////////////

void print_wav_info() {
  
  Serial.println(" ");
  int x = FILECOUNT;
  while (x) {
    x--;
    Serial.print(DISPLAYFILES[x]);
    Serial.print(" <-- ");
    Serial.println(FILES[x]);
  }
  Serial.println("");
  Serial.print("total: # "); Serial.println(FILECOUNT);
  // to do: file len, ctrl res etc: FILE_LEN[MAXFILES*2]; etc 
  Serial.println("ok");
}


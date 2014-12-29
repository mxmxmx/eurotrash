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
        audioChannels[i]->pos0 = 0;
        audioChannels[i]->pos1 = CTRL_RESOLUTION;
        audioChannels[i]->ctrl_res = CTRL_RES[_file];
        audioChannels[i]->ctrl_res_eof = CTRL_RES_EOF[_file];
        audioChannels[i]->eof = CTRL_RESOLUTION * CTRL_RES_EOF[_file];
        audioChannels[i]->_gain = DEFAULT_GAIN;  
        audioChannels[i]->swap = false;
  } 
}  

/* ====================check clocks================ */

void leftright() {
  
 if (LCLK) {  // clock?
  
       play_x(LEFT);
       LCLK = false;
       FADE_LEFT = false;
       last_LCLK = millis();
 
   } 
  if (RCLK) { // clock?
 
       play_x(RIGHT);
       RCLK = false;
       FADE_RIGHT = false;
       last_RCLK = millis();
 
   } 
}

/* =============================================== */

void play_x(uint8_t _channel) {
  
      uint8_t _swap, _select;
      _swap   = audioChannels[_channel]->swap;                
      _select = (_channel*CHANNELS) + _swap;                 // select audio object # 1,2 (LEFT) or 3,4 (RIGHT)
      fade[_select]->fadeIn(FADE_IN);                        // fade in object 1-4
      next_wav(_select, _channel);                           // and play 
  
      /* now swap the file and fade out previous file: */
      _swap = ~_swap & 1u;
      _select = (_channel*CHANNELS) + _swap;
      fade[_select]->fadeOut(FADE_OUT);
      audioChannels[_channel]->swap = _swap;
  
}
 
/* =============================================== */

void next_wav(uint8_t _select, uint8_t _channel) {
  
       /* file */
       uint16_t _file = audioChannels[_channel]->file_wav; 
       /* where to start from? */
       int16_t _CV = (HALFSCALE - CV[3-_channel])>>5;  
       int16_t  _startPos =  _CV + audioChannels[_channel]->pos0; 
       /* limit */
       if (_startPos < 0) _startPos = 0;
       else if (_startPos >= CTRL_RESOLUTION) _startPos = CTRL_RESOLUTION-1;
       /* scale */
       uint32_t _playpos =  _startPos * audioChannels[_channel]->ctrl_res; 
       /* filename */
       String playthis = FILES[_file];  
       /* -> play file X from pos Y */
       wav[_select]->seek(&playthis[0], _playpos>>9); 
       /* now update channel data: */
       audioChannels[_channel]->ctrl_res = CTRL_RES[_file];
       audioChannels[_channel]->ctrl_res_eof = CTRL_RES_EOF[_file];
       
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
                      
                      FILES[FILECOUNT] = _name;
                      //file_len  = thisfile.size() - 0x2e; // size minus header [ish]
                      /* this is annoying */
                      wav1.play(_name);
                      delay(15);
                      file_len = (float)wav1.lengthBytes() * 0.9f;
                      //FILE_LEN[FILECOUNT]  = file_len;
                      CTRL_RES[FILECOUNT]  = file_len / CTRL_RESOLUTION;       // ctrl resolution pos0/bytes/
                      file_len_ms = wav1.lengthMillis();
                      CTRL_RES_EOF[FILECOUNT] = file_len_ms / CTRL_RESOLUTION; // ctrl resolution pos1/millisec
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
                      DISPLAYFILES[FILECOUNT] = tmp;
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
       int16_t _CV, tmp, tmp2; 
       _CV = (HALFSCALE - CV[_channel])>>5;                    // CV
       tmp  = audioChannels[_channel]->pos1 + _CV;             // length
       tmp2 = CTRL_RESOLUTION - audioChannels[_channel]->pos0; // max length
       if (tmp > tmp2) tmp = tmp2;
       else if (tmp <= 1) tmp = 1;
       audioChannels[_channel]->eof = tmp * audioChannels[_channel]->ctrl_res_eof;
     
    }
}  

/* =============================================== */

void calibrate() {
  
  /*  calibrate mid point */
      float average;
      uint8_t save = false;
      HALFSCALE = 0;
      MENU_PAGE[LEFT]  = MODE;
      MENU_PAGE[RIGHT] = MODE;
      update_display(LEFT,   HALFSCALE);
      delay(1000);
      for (int i = 0; i < 200; i++) {
   
           average +=  analogRead(CV1);
           average +=  analogRead(CV2);
           average +=  analogRead(CV3);
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
      BUTTON = false;  
} 


/* some stuff to save the ADC calibration value */

void writeMIDpoint(uint16_t _val) {
   
  uint8_t byte0, byte1, adr;
       
       byte0 = _val >> 8;
       byte1 = _val;
       EEPROM.write(adr, 0xFF);
       adr++;
       EEPROM.write(adr, byte0);
       adr++;
       EEPROM.write(adr, byte1);
}  

uint16_t readMIDpoint() {
  
       uint8_t byte0, byte1, adr = 0x1;
       byte0 = EEPROM.read(adr);
       adr++;
       byte1 = EEPROM.read(adr);
       
       return  (uint16_t)(byte0 << 8) + byte1;
}  


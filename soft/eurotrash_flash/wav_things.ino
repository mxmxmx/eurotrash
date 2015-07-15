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
     
      // fade in voice: 
      fade[_numVoice + _bank*0x4]->fadeIn(FADE_IN);
       
       if (_bank) { // spi flash
             uint16_t _file   = _channel->file_wav;
             raw[_numVoice]->seek(FILES[MAXFILES+_file], (_startPos >> 8) << 8); // startPos ~ pages
       }
       else { // SD
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
      
         uint16_t  _id, _bank, _file;
         
         _id   = _channel->id*CHANNELS;
         _bank = _channel->bank*MAXFILES;
         _file = _channel->file_wav;
         
         if (_bank == _SD) { 
         
             const char *_file_name = FILES[_file];
             // close files : 
             wav[_id]->close();     
             wav[_id+0x1]->close(); 
             // open new files : 
             wav[_id]->open(_file_name);
             wav[_id+0x1]->open(_file_name);
         }
         //  update channel data: 
         _channel->ctrl_res = CTRL_RES[_file + _bank];
         _channel->ctrl_res_eof = CTRL_RES_EOF[_file + _bank]; 
         _channel->swap  = 0x0;     // reset
         _channel->state = _PLAY;  
    }
}

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

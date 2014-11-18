/*
*
*  UI : display / encoders
*
*/

#define MAXFILES 128                 // we don't allow more than 128 files (for no particular reason); banks? (but would just add more menu pages)
uint8_t FILECOUNT;
const uint8_t DISPLAY_LEN = 9;       // 8 (8.3) + 1 (active file indicator)
String FILES[MAXFILES]; 
String DISPLAYFILES[MAXFILES];
//uint32_t FILE_LEN[MAXFILES];
uint32_t CTRL_RES[MAXFILES];
uint32_t CTRL_RES_EOF[MAXFILES];
float DEFAULT_GAIN = 0.4;            // adjust default volume [0.0 - 1.0]

uint8_t ENCODER_SWAP, DIR;           // alternate reading the encoders
const uint8_t CTRL_RESOLUTION = 100; // ctrl resolution (encoders), relative to file size; adjust to your liking (< 9999)
uint8_t SLOW = 0;                    // encoder response
int16_t prev_encoderdata[]  = {-999, -999};

String leftdisplay  = "      0"; 
String rightdisplay = "      0"; 
uint8_t MENU_PAGE[2] = {0,0};
uint8_t filedisplay[2];

/* menu pages */
enum { 
 FILESELECT,
 STARTPOS,
 ENDPOS,
 MODE // unused
};
  
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
        encoderdata = encoder[_channel].pos()>>SLOW;
        update_display(_channel, encoderdata);
        
        /* update EOF */
        if (MENU_PAGE[_channel] != FILESELECT) {
          
              uint32_t tmp, tmp2; 
              tmp  = audioChannels[_channel]->pos1;                   // length
              tmp2 = CTRL_RESOLUTION - audioChannels[_channel]->pos0; // max length
              if (tmp > tmp2) tmp = tmp2;
              audioChannels[_channel]->eof = tmp * audioChannels[_channel]->ctrl_res_eof;
        }
  }  
}  

/* --------------------------------------------------------------- */

void buttons(uint8_t _channel) {
  
  uint8_t _ch = _channel;
  switch (MENU_PAGE[_ch]) {
    
  case FILESELECT: { // update file  
  
          uint8_t _file = audioChannels[_ch]->file_wav;
          if (filedisplay[_ch] != _file) update_channel(audioChannels[_ch]);
          else {
              MENU_PAGE[_ch] = STARTPOS;
              int16_t start_pos = audioChannels[_ch]->pos0;
              encoder[_channel].setPos(start_pos<<SLOW);
              update_display(_ch, start_pos);
          }    
          break;
  }
  
  case STARTPOS: { // start pos
  
          MENU_PAGE[_ch] = ENDPOS;
          int16_t end_pos = audioChannels[_ch]->pos1;
          encoder[_channel].setPos(end_pos<<SLOW);
          update_display(_ch, end_pos);
          break; 
  } 
  
  case ENDPOS: { // end pos
  
          MENU_PAGE[_ch] = FILESELECT;
          int8_t _file = audioChannels[_ch]->file_wav;
          encoder[_channel].setPos(_file<<SLOW);
          update_display(_ch, _file);
          break; 
  }     
  
  case MODE: {
          
          break;
        
   }
     
  default: break;  
 } 
}  

/* --------------------------------------------------------------- */

void update_channel(struct audioChannel* _ch) {
        
        uint8_t _id   = _ch->id;    // L or R ?
        uint8_t _file = filedisplay[_id];
        _ch->file_wav = _file;      // select file
        //_ch->file_len = FILE_LEN[_file];
        update_display(_id, _file); // update menu

}  

/* --------------------------------------------------------------- */

String makedisplay(int16_t number) {
    
    String tmp;
    if (number > 999)      { tmp = "     "; tmp.concat(number); }
    else if (number > 99)  { tmp = "      "; tmp.concat(number); }
    else if (number > 9)   { tmp = "       "; tmp.concat(number); }
    else if (number >= 0)  { tmp = "        "; tmp.concat(number); }
    else tmp = "        0"; 
    return tmp;
} 

/* --------------------------------------------------------------- */

void update_display(uint8_t _channel, uint16_t _newval) {
  
 String msg;
 int16_t tmp = _newval;
 char cmd = 0x02;      // this is the cmd byte / prefix for the Serial messages (ascii starting at 0x02).
 switch (MENU_PAGE[_channel]) {
   
     case FILESELECT: { // file
           uint8_t   tmp2 = FILECOUNT-1;
           if (tmp < 0)  {
                 tmp = tmp2; 
                 encoder[_channel].setPos(FILECOUNT<<SLOW); 
                 DIR = false;
             }
           else if (tmp > tmp2 && DIR) {
                tmp = 0; encoder[_channel].setPos(0); 
             }
           else DIR = true;
           if (tmp > tmp2) tmp = tmp2;
           msg = (DISPLAYFILES[tmp]);
           if (tmp == audioChannels[_channel]->file_wav) msg[0] = '\xb7';
           filedisplay[_channel] = tmp;  
           break;
     }
   
     case STARTPOS: {  
            if (tmp < 0) tmp = 0;
            else if (tmp > CTRL_RESOLUTION) { tmp = CTRL_RESOLUTION; encoder[_channel].setPos(CTRL_RESOLUTION<<SLOW);}
            audioChannels[_channel]->pos0 = tmp;
            msg = makedisplay(tmp);
            cmd +=0x02;
            break;
          
     }
     case ENDPOS: {
           if (tmp < 1) tmp = 1;
           else if (tmp > CTRL_RESOLUTION) { tmp = CTRL_RESOLUTION; encoder[_channel].setPos(CTRL_RESOLUTION<<SLOW);}
           audioChannels[_channel]->pos1 = tmp;
           msg = makedisplay(tmp);
           cmd +=0x04;
           break;
     }  
     
     case MODE: {
          
           break;
        
     }
     
     default: break;  
 }
 
 // send to atmega 
 cmd += _channel;
 HWSERIAL.print(cmd + msg);
}



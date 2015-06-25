
// main loop : 

void _loop() {
 
     leftright();
     // fade out voice ?
     if (!FADE_LEFT) eof_left();  

     leftright();
     // fade out voice ?
     if (!FADE_RIGHT) eof_right(); 
     
     leftright();
   
     if (UI) _UI();                 
   
     leftright();
   
     if (_ADC) _adc();             
   
     leftright();
     // end-of-file ? pause streaming 
     if (EOF_L_OFF) _pause_active_L();   
   
     leftright();
     // end-of-file ? pause streaming 
     if (EOF_R_OFF) _pause_active_R();  
     
     leftright();
     // new file ?
     if (!audioChannels[LEFT]->_open)  _open_next(audioChannels[LEFT]); 
     
     leftright();
     // new file ?
     if (!audioChannels[RIGHT]->_open) _open_next(audioChannels[RIGHT]); 
     // pause streaming ? (inactive voice) 
     if (PAUSE_FILE_L) _pause_inactive_L(); 
     
     leftright();
     // pause streaming ? (inactive voice)
     if (PAUSE_FILE_R) _pause_inactive_R();  
  
}

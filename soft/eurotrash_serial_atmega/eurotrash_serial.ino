/**********************************************************
*
* receive & display
*
* soft spi: SCK = PB0 = 8; MOSI = PD6 = 6; CS = PC1 = A1; RST = PC0 = A0; D/C = PB = 10)
*
* file names are 8.3, so MSG_SIZE == 4 (menu item) + 8 (name) + '\xb7' (active file) + '\0' = 14 
*
***********************************************************/

#include "U8glib.h"
U8GLIB_SSD1306_128X32 u8g(8, 6, A1, 10, A0);	

#define MSG_SIZE 14    // display
#define MSG_SIZE_RX 10 
#define MENU_ITEMS 8
uint8_t  menu;
char msgL[MSG_SIZE], msgR[MSG_SIZE];


void draw(void) {
 
    u8g.setPrintPos(6, 15); 
    u8g.print(msgL);
    u8g.setPrintPos(6, 30);
    u8g.print(msgR);
}

/* --------------------------------------- */

void setup(void) {
  
  delay(200);
  u8g.setFont(u8g_font_unifont);
  delay(200);
  Serial.begin(115200);
  
}
/* --------------------------------------- */

 

void loop(void) {
  
 if (Serial.available() >= (MSG_SIZE_RX)) {
   
    byte cmd = Serial.read();
    parse(cmd);
  }
  
 if (!menu) {
    menu = 1;
    u8g.firstPage();  
    do {
        draw();
    } while( u8g.nextPage() );
  }
}



void parse(byte _cmd) {
  
  menu = 0;

  switch (_cmd) {
    
  case 0x02: { // file left
       
       msgL[0] = 'L';
       msgL[1] = ' ';
       msgL[2] = '>';
       msgL[3] = ' ';
       msgL[4] =   Serial.read();  // active file?
       msgL[5] =   Serial.read();  // 8.3 :
       msgL[6] =   Serial.read();
       msgL[7] =   Serial.read();
       msgL[8] =   Serial.read();
       msgL[9] =   Serial.read();
       msgL[10] =  Serial.read();
       msgL[11] =  Serial.read();
       msgL[12] =  Serial.read();
       msgL[13] =  '\0'; 
       
       break;
        
   }
   case 0x03:   {  // file right
        
      msgR[0] = 'R'; 
      msgR[1] = ' ';   
      msgR[2] = '>'; 
      msgR[3] = ' ';     
      msgR[4] =   Serial.read();
      msgR[5] =   Serial.read();
      msgR[6] =   Serial.read();
      msgR[7] =   Serial.read();
      msgR[8] =   Serial.read();
      msgR[9] =   Serial.read();
      msgR[10] =  Serial.read();
      msgR[11] =  Serial.read();
      msgR[12] =  Serial.read();
      msgR[13] =  '\0';
      
      break;
   }
   
   case 0x04:    { // start pos L
       msgL[0] = '0';
       msgL[1] = ' ';
       msgL[2] = '>';
       msgL[3] = ' ';
       msgL[4] =   Serial.read();  
       msgL[5] =   Serial.read();  
       msgL[6] =   Serial.read();
       msgL[7] =   Serial.read();
       msgL[8] =   Serial.read();
       msgL[9] =   Serial.read();
       msgL[10] =  Serial.read();
       msgL[11] =  Serial.read();
       msgL[12] =  Serial.read();
       msgL[13] =  '\0'; 
       break;
          
   }      
   case 0x05:   { // start pos R
        
        msgR[0] = '0'; 
        msgR[1] = ' ';   
        msgR[2] = '>'; 
        msgR[3] = ' '; 
        msgR[4] =   Serial.read();
        msgR[5] =   Serial.read();
        msgR[6] =   Serial.read();
        msgR[7] =   Serial.read();
        msgR[8] =   Serial.read();
        msgR[9] =   Serial.read();
        msgR[10] =  Serial.read();
        msgR[11] =  Serial.read();
        msgR[12] =  Serial.read();
        msgR[13] =  '\0';
          break;
          
   }   
  
   case 0x06:    { // end pos L
         
       msgL[0] = 'E';
       msgL[1] = 'O';
       msgL[2] = 'F';
       msgL[3] = ' ';
       msgL[4] =   Serial.read();  
       msgL[5] =   Serial.read();  
       msgL[6] =   Serial.read();
       msgL[7] =   Serial.read();
       msgL[8] =   Serial.read();
       msgL[9] =   Serial.read();
       msgL[10] =  Serial.read();
       msgL[11] =  Serial.read();
       msgL[12] =  Serial.read();
       msgL[13] =  '\0'; 
       break;
          
   }
   case 0x07:    { // end pos R
      msgR[0] = 'E'; 
      msgR[1] = 'O';   
      msgR[2] = 'F'; 
      msgR[3] = ' '; 
      msgR[4] =   Serial.read();
      msgR[5] =   Serial.read();
      msgR[6] =   Serial.read();
      msgR[7] =   Serial.read();
      msgR[8] =   Serial.read();
      msgR[9] =   Serial.read();
      msgR[10] =  Serial.read();
      msgR[11] =  Serial.read();
      msgR[12] =  Serial.read();
      msgR[13] =  '\0';
      break;
          
    }   
    case 0x08:    { // vol L
       
       msgL[0] = 'V';
       msgL[1] = 'O';
       msgL[2] = 'L';
       msgL[3] = ' ';
       msgL[4] =   Serial.read();  
       msgL[5] =   Serial.read();  
       msgL[6] =   Serial.read();
       msgL[7] =   Serial.read();
       msgL[8] =   Serial.read();
       msgL[9] =   Serial.read();
       msgL[10] =  Serial.read();
       msgL[11] =  Serial.read();
       msgL[12] =  Serial.read();
       msgL[13] =  '\0'; 
       break;
          
    }  
    case 0x09:    { // vol R
        msgR[0] = 'V'; 
        msgR[1] = 'O';   
        msgR[2] = 'L'; 
        msgR[3] = ' '; 
        msgR[4] =   Serial.read();
        msgR[5] =   Serial.read();
        msgR[6] =   Serial.read();
        msgR[7] =   Serial.read();
        msgR[8] =   Serial.read();
        msgR[9] =   Serial.read();
        msgR[10] =  Serial.read();
        msgR[11] =  Serial.read();
        msgR[12] =  Serial.read();
        msgR[13] =  '\0';
        break;
          
    }   
  }
}


 

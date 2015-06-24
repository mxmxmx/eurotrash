/*
*
* spi flash utilities. with special thanks to Frank B.
* ...
*/

#define FLASHSIZE 0x1000000
#define PAGE      256
#define DIRECTORY "SERFLASH"
#define MAX_LEN 0x8                    // file name size
#define INFO_SLOT_SIZE (MAX_LEN + 0x4) // address size + file name
#define _OFFSET 0x6                    // file # in bytes 0,1
#define INFO_ADR 0x0                   // info adr.

uint8_t _EXT = false;

/*  ======================================== */

void generate_file_list_flash(void) {
 
        uint16_t _pos = 0;
        float len, frames;
        while (_pos < RAW_FILECOUNT) {
          
           raw1.play(RAW_FILE_ADR[_pos]);  // open file
           delay(15);        
           len = RAW_FILE_ADR[_pos+1] - RAW_FILE_ADR[_pos];   // file length (bytes)
           frames = 2*raw1.SamplesConsumedPerUpdate();        // file length (frames)
           len = len / frames;
           CTRL_RES[MAXFILES + _pos] = len * CTRL_RESOLUTION_INV + 0x01; // ctrl res in frames
           CTRL_RES_EOF[MAXFILES + _pos] = (float)raw1.lengthMillis() * CTRL_RESOLUTION_INV; 
           _pos++;     
           raw1.stop();    
    }
}

/*  ======================================== */

uint8_t spi_flash_init() {
 
  unsigned char id_tab[32];
  int flashstatus;
  flashstatus = raw1.flash_status();
  raw1.spififo_flash_read_id(id_tab);
  Serial.printf("Flash Status: 0x%X, ID:0x%X,0x%X,0x%X,0x%X ", flashstatus , id_tab[0], id_tab[1], id_tab[2], id_tab[3]);   
  if (id_tab[0]!=0xef || id_tab[1]!=0x40 || id_tab[2]!=0x18 || id_tab[3]!=0x00) flashstatus = 0x0;
  else flashstatus  = 0x1; // flash ok
  Serial.println("");
  return flashstatus;
}

/*  ======================================== */

void info() {
  
  Serial.println(" ");
  int x = SPI_FLASH_STATUS;
  while (x) {
  x--;
  Serial.print(RAW_DISPLAYFILES[x]);
  Serial.print(" / ");
  Serial.print(RAW_FILE_ADR[x], HEX);
  Serial.print(" / ");
  Serial.print(CTRL_RES[x+MAXFILES], DEC);
  Serial.print(" / ");
  Serial.println(CTRL_RES_EOF[x+MAXFILES], DEC);
  }
  Serial.print("total: # "); Serial.println(RAW_FILECOUNT);
  // to do: file len, ctrl res etc: FILE_LEN[MAXFILES*2]; etc 
  Serial.println("ok");
}

uint8_t spi_flash(){
  
    MENU_PAGE[LEFT]  = FLASH;
    MENU_PAGE[RIGHT] = FLASH;
    
    uint16_t _files;
    if (!SPI_FLASH_STATUS) { // flash is not ok 
          update_display(LEFT, FLASH_NOT_OK);
          delay(4000);
          _files = 0x0;
    }
    else {  // check SD/SERFLASH :
          update_display(LEFT, FLASH_OK);
          delay(1000);
          _files = read_SDFLASH_dir();
          
          if (_files) {  // we found files ... 
                
                 update_display(RIGHT, FCNT);
                 
                 if (!verify(_files)) {  // no files on flash or files differ ...
                         
                         delay(1000);
                         update_display(LEFT, ERASE);
                         update_display(RIGHT, ALLGOOD);
                         //delay(1000);
                         erase();
                         update_display(LEFT, FLASHING);
                         update_display(RIGHT, ALLGOOD);
                   
                         _files = flash(_files);   
                         delay(1500);
                        _files = verify(_files); 
                        if (_files)  { 
                                    update_display(LEFT, FLASH_OK);
                                    update_display(RIGHT, FCNT);
                        }
                        else        update_display(LEFT, FLASH_NOT_OK);
                        delay(2000);
                } 
                else { // nothing to do
                         update_display(LEFT,  ALLGOOD);
                         update_display(RIGHT, ALLGOOD);
                         delay(2000);  
                }
          }
          else { // nothing found on SD or files too large
                   update_display(LEFT, SD_ERROR);
                   delay(4000);   
          }
    }      
    delay(1000);
    MENU_PAGE[LEFT]  = FILESELECT; 
    MENU_PAGE[RIGHT] = FILESELECT; 
    LASTBUTTON = millis(); 
    return _files;
}

/*  ======================================== */

uint16_t read_SDFLASH_dir(void) {
  
    File dir;
    File entry;
    uint32_t fsize = 0;
    uint16_t fcnt = 0;
    String filename[MAXFILES];
    
    dir = SD.open(DIRECTORY);
    
    while(fcnt < MAXFILES) {
         entry = dir.openNextFile();
         if (!entry || fcnt >= MAXFILES) break;    
         int s = entry.size();   
         if ( (s & 0xff) > 0) s |= 0xff;
         filename[fcnt] = entry.name(); // --> filenames
         Serial.printf("%s\t\t\t%d bytes\r\n", filename[fcnt].c_str(), s);
         fsize += s;   
         entry.close();  
         fcnt++;
  } 
  dir.close();
  uint16_t num_pages = 0x1 + ((fcnt*INFO_SLOT_SIZE + _OFFSET) >> 0x8); // == # info pages; 12 bytes: pos + name + fcnt;
  fsize += num_pages*PAGE;
  if (fsize < FLASHSIZE) return fcnt;
  else return 0x0; 
}

/*  ======================================== */

uint16_t verify(uint16_t fcnt_all)
{
    File dir;
    File entry;
    unsigned char buf[PAGE];
    unsigned char buf2[PAGE];
    uint16_t fcnt = 0;
    uint32_t pos = 0; 
    uint16_t page = 0;   
    Serial.println("Verify.");
    // file number?
    raw1.spififo_flash_read_pages(buf, INFO_ADR, 1);
    uint16_t num_files = ((uint16_t)(buf[0]) << 0x8) + buf[1]; // extract file #
    uint16_t num_pages = 0x1 + ((num_files*INFO_SLOT_SIZE + _OFFSET) >> 0x8); // page offset
   
    if (fcnt_all != num_files)  { 
              Serial.println("Files on flash ? --> file #: no match"); 
              Serial.println(""); 
              return false; 
    }
    else Serial.printf("Files on flash ? --> # %d file(s): ok \r\n", num_files); 
    
    page += num_pages;   // audio data starts here
   
    dir = SD.open(DIRECTORY); 
    while(fcnt < MAXFILES) {
        entry = dir.openNextFile();
        if (!entry) break;
        pos = page * PAGE;
        Serial.printf("%d. Verifying \"%s\" at file_position: 0x%07X...", fcnt+1, entry.name(), pos);
        int rd =0;
        do {
            memset(buf, 0xff, PAGE);
            rd = entry.read(buf, PAGE);
            raw1.spififo_flash_read_pages(buf2, page, 1);
            int v = 0;
            for (int i=0; i<PAGE; i++) v+= buf[i]-buf2[i];
            if (v) {Serial.println("no match");  dir.close(); return 0x0;}
            pos += rd;         
            page++;
        } while (rd==PAGE);          
        Serial.printf("ok.\r\n");
        entry.close(); 
        fcnt++;
    }
    dir.close();
    return fcnt;
}

/*  ======================================== */

uint16_t flash(uint16_t num_files){
  
    File dir;
    File entry;
    unsigned char buf[PAGE];
    uint16_t fcnt = 0;
    uint32_t pos  = 0;
    uint16_t NUM_INFO_PAGES = 0x1 + ((num_files*INFO_SLOT_SIZE + _OFFSET) >> 0x8); // audio data starts here
    uint16_t page = NUM_INFO_PAGES;
    uint32_t file_position[MAXFILES];
    String filename[MAXFILES];
      
    dir = SD.open(DIRECTORY);
    
    while(fcnt < num_files) {
      
        entry = dir.openNextFile();
        if (!entry) break;
        pos = page * PAGE;
        Serial.printf("-- > Flashing file # %d : \"%s\" at file_position: 0x%07X ...\r\n", fcnt+1, entry.name(), pos);
        file_position[fcnt] = pos; // store file_position
        filename[fcnt] = entry.name();
        int rd =0;
        do {
              memset(buf, 0xff, PAGE);          
              rd = entry.read(buf, PAGE);
              pos += rd;         
              raw1.spififo_flash_page_program(buf,page);
              page++;
      } while (rd==PAGE);          
    entry.close(); 
    fcnt++;
    }
    // store INFO_PAGES:
    uint16_t slot = 0;
    uint32_t end_of_data = page * PAGE;
    uint8_t pseudo_file_sys[NUM_INFO_PAGES*PAGE]; 
    Serial.println("");
    Serial.printf("Generating file info (%d page(s)):", NUM_INFO_PAGES);
    memset(pseudo_file_sys, 0xff, NUM_INFO_PAGES*PAGE); 
    // file # 
    pseudo_file_sys[slot] = fcnt>>8;         // 0
    slot++;
    pseudo_file_sys[slot] = fcnt;            // 1
    slot++;
    pseudo_file_sys[slot] = end_of_data>>24; // 2
    slot++; 
    pseudo_file_sys[slot] = end_of_data>>16; // 3
    slot++;
    pseudo_file_sys[slot] = end_of_data>>8;  // 4
    slot++;
    pseudo_file_sys[slot] = end_of_data;     // 5
    slot++;
    Serial.println("");
    // adress + file-names
    uint8_t len = MAX_LEN; 
    for (int i = 0; i < fcnt; i++) {
       
        uint32_t f_pos = file_position[i];
        // split address:
        pseudo_file_sys[slot] = f_pos>>24;
        slot++;
        pseudo_file_sys[slot] = f_pos>>16;
        slot++;
        pseudo_file_sys[slot] = f_pos>>8;
        slot++; 
        pseudo_file_sys[slot] = f_pos;
        slot++;
        Serial.printf("-- > file # %d (of %d) :: 0x%07X ...\r\n", i, fcnt, f_pos);
        // write name (8.3) -- skip the extension
        _EXT = 0x0;
        const char *tmp = filename[i].c_str();
        for (int j = 0; j < len; j++) {
             pseudo_file_sys[slot] = makenice(tmp[j], _EXT);
             slot++;
        }
    }    
    // now flash the info page(s):
    uint8_t pages = NUM_INFO_PAGES; 
    page = INFO_ADR; // start from 0x0
    Serial.println("");   
    while(pages) {
        Serial.printf("-- > Flashing info page # %d (of %d) at file_position: 0x%07X ...\r\n", page+1, NUM_INFO_PAGES, page*PAGE);
        uint8_t *sys = pseudo_file_sys+page*PAGE;
        raw1.spififo_flash_page_program(sys, page);
        pages--;
        page++;
    }
    Serial.printf("done.\r\n");
    Serial.println("");
    return fcnt;  
}

/*  ======================================== */

uint16_t extract(void)
{
    uint16_t num_files;
    uint16_t num_pages;
    uint32_t end_of_data;
    unsigned char buf[PAGE];
    // file number?
    raw1.spififo_flash_read_pages(buf, INFO_ADR, 1);
    num_files = ((uint16_t)(buf[0]) << 0x8) + buf[1];               // extract file #
    
    if (!num_files || num_files > MAXFILES)  { 
              //Serial.println("-->  no files found"); 
              return false; 
    }
   
    end_of_data = (buf[2] << 24) + (buf[3] << 16) + (buf[4] << 8) + buf[5];
    // ... ok, now, read file info:
    num_pages = 0x1 + ((num_files*INFO_SLOT_SIZE + _OFFSET) >> 0x8);  // info-page size
    unsigned char tmp_buf[PAGE*num_pages];
    raw1.spififo_flash_read_pages(tmp_buf, INFO_ADR, num_pages);
    parse_INFO_PAGES(tmp_buf, num_files); 
  
    RAW_FILECOUNT = num_files;
    // calc. file lengths:
    RAW_FILE_ADR[num_files] = end_of_data;
    /*
    uint16_t _pos = 0;
    while (_pos < RAW_FILECOUNT) {
   
           CTRL_RES[MAXFILES + _pos]   = (RAW_FILE_ADR[_pos+1] - RAW_FILE_ADR[_pos])/CTRL_RESOLUTION; // in bytes
           raw1.play(RAW_FILE_ADR[_pos]); delay(15);  
           CTRL_RES_EOF[MAXFILES +_pos] = raw1.lengthMillis() / CTRL_RESOLUTION; 
           _pos++;         
    }
    */
    return num_files;
}

/*  ======================================== */

void parse_INFO_PAGES(uint8_t *fileinfo, uint8_t _files) {
  
     
     fileinfo += _OFFSET; // skip file # bytes
     uint16_t _f = _files;
     Serial.println("");
     while (_f) {
       
         uint32_t tmp, adr;
         tmp = *fileinfo; fileinfo++;   // 1
         adr  = tmp << 24; 
         tmp = *fileinfo; fileinfo++;   // 2
         adr += tmp << 16;
         tmp = *fileinfo; fileinfo++;   // 3
         adr += tmp << 8;
         tmp = *fileinfo; fileinfo++;   // 4
         adr += tmp;
         
         RAW_FILE_ADR[_files-_f] = adr; // file pos
         
         uint8_t len = MAX_LEN;         // names
         char _name[DISPLAY_LEN+0x1]; 
         while (len) {  
               _name[MAX_LEN-len] = *fileinfo;
               len--;   
               fileinfo++;  
         };  
         right_justify(_name);
         memcpy(RAW_DISPLAYFILES[_files-_f], _name, sizeof(_name));
         Serial.printf(" -- > RAW_FILE_ADR[%d] = 0x%07X ......\r\n", _files - _f, adr);
        _f--;
     }
};

/*  ======================================== */

char makenice(char _n, uint8_t _ext) {
   // TD: preserve numbers
   if (_ext) return ' ';
   
   char tmp = _n;   
   if (tmp == '.') _EXT = 1; 
   if (tmp >= '0' && tmp <= '9') tmp = tmp;
   else if (tmp >= 'A' && tmp  <= 'Z' ) tmp = tmp + 'a' - 'A';
   else tmp = ' ';
   return tmp;
}

/*  ======================================== */

void right_justify(char *_name) {
           
    uint8_t _offset = 0, _pos = 0, _justify = 0;
    
    char _tmp[DISPLAY_LEN+0x1];
    memcpy(_tmp, _name, MAX_LEN);
    
    while(_pos < MAX_LEN) {         
          if (_name[_pos] == ' ') _offset++;
          _pos++;
    } 
    _justify = _offset;
    _pos = 0;     
    while (_offset < DISPLAY_LEN) { 
          _name[_offset+1] = _tmp[_pos];
          _pos++; _offset++;
    }
    _pos = 0; 
    while(_pos <= _justify) {    
          _name[_pos] = ' ';
          _pos++; //
    }
     _name[DISPLAY_LEN] = '\0';
}

/*  ======================================== */

void erase(void) {
    Serial.println("Erasing chip....");
    raw1.spififo_flash_chip_erase(true);
    Serial.println("done.");
}


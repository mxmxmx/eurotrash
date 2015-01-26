/*
   W25Q128FV Serial Flasher
   
   (c) Frank Boesing, 2014,Dec
   GNU License Version 3.
   Teensy Audio Shield (c) Paul Stoffregen 
   W25Q128FV - Library  (c) Pete (El Supremo) 
   Thank you both!!!!
      
   Reads directory "/SERFLASH" on SD and burns 
   all files to the optional serial flash.
  
   Version 20141227
   
   + file info / extraction (mxmxmx)
      
*/

#include <Audio.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <flash_spi.h>

#define FLASHSIZE 0x1000000
#define PAGE      256
#define DIRECTORY "SERFLASH"
#define MAX_FILES 256 
#define MAX_LEN 0x8 // file name size
#define INFO_SLOT_SIZE MAX_LEN + 0x4 // address size + file name
#define PAGE_OFFSET 0x2 // file # in bytes 0,1

uint8_t INFO_PAGES; 

File dir;
File entry;
unsigned char id_tab[32];
unsigned pos;
unsigned page;
int fsize = 0;
int fcnt, fcnt_all = 0;
unsigned char buf[PAGE];

String filename[MAX_FILES];
unsigned int file_position[MAX_FILES];
uint32_t file_adr[MAX_FILES];
char recovered_names[MAX_FILES][MAX_LEN];
uint8_t _EXT = 0;


bool verify(void)
{
  
    unsigned char buf2[PAGE];

    fcnt = 0;
    pos = 0; 
    page = 0;   
    Serial.println("Verify.");
    // file number?
    flash_read_pages(buf2, page, 1);
    uint16_t files_nr = ((uint16_t)(buf2[0]) << 0x8) + buf2[1]; // extract file #
    uint8_t  page_offset = 0x1 + ((files_nr*INFO_SLOT_SIZE + PAGE_OFFSET) >> 0x8); // page offset
   
    if (fcnt_all != files_nr)  { 
              Serial.println("Files on flash ? --> file #: no match"); 
              Serial.println(""); 
              return false; 
    }
    else Serial.printf("Files on flash ? --> # %d file(s): ok \r\n", files_nr); 
    
    page += page_offset;  
   
    dir = SD.open(DIRECTORY); 
    while(1) {
      entry = dir.openNextFile();
      if (!entry) break;
      pos = page * PAGE;
      Serial.printf("%d. Verifying \"%s\" at file_position: 0x%07X...", fcnt+1, entry.name(), pos);
      filename[fcnt] = entry.name();
      file_position[fcnt] = pos;
      int rd =0;
      do {
        memset(buf, 0xff, PAGE);
        rd = entry.read(buf, PAGE);
        flash_read_pages(buf2, page, 1);
        int v = 0;
        for (int i=0; i<PAGE; i++) v+= buf[i]-buf2[i];
        if (v) {Serial.println("no match"); return false;}
        pos += rd;         
        page++;
      } while (rd==PAGE);          
      Serial.printf("ok.\r\n");
      entry.close(); 
      fcnt++;
    }
    return true;
}

void flash(void)
{
  
   unsigned char buf[PAGE];
    
   fcnt = 0;
   pos =  0;
   page = INFO_PAGES; // file info offset 
    
   dir = SD.open(DIRECTORY);
   while(fcnt < fcnt_all) {
      entry = dir.openNextFile();
      if (!entry) break;
      pos = page * PAGE;
      Serial.printf("-- > Flashing file # %d : \"%s\" at file_position: 0x%07X ...\r\n", fcnt+1, entry.name(), pos);
      file_position[fcnt] = pos; // store file_position
      int rd =0;
      do {
          memset(buf, 0xff, PAGE);          
          rd = entry.read(buf, PAGE);
          pos += rd;         
          flash_page_program(buf,page);
          page++;
      } while (rd==PAGE);          
    entry.close(); 
    fcnt++;
    }
    
    // store INFO_PAGES:
    page = 0;
    uint16_t slot = 0;
    uint8_t pseudo_file_sys[INFO_PAGES*PAGE]; 
    Serial.println("");
    Serial.printf("Generating file info (%d page(s)):", INFO_PAGES);
  
    memset(pseudo_file_sys, 0xff, INFO_PAGES*PAGE); 
    // file #  
    pseudo_file_sys[slot] = fcnt_all>>8;
    slot++;
    pseudo_file_sys[slot] = fcnt_all;
    slot++;
    Serial.println("");
    // adr + name
    uint8_t len = MAX_LEN; // name len
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
        Serial.printf("-- > file # \%d\ (of %d) :: 0x%07X ...\r\n", i, fcnt, f_pos);
        // write name (8.3) -- skip the extension
        _EXT = 0; //
        const char *tmp = filename[i].c_str();
        for (int j = 0; j < len; j++) {
             pseudo_file_sys[slot] = makenice(tmp[j], _EXT);
             slot++;
        }
    }    
    // now flash the info page(s):
    uint8_t pages = INFO_PAGES; 
    Serial.println("");   
    while(INFO_PAGES) {
        Serial.printf("-- > Flashing info page # \%d\ (of %d) at file_position: 0x%07X ...\r\n", page+1, pages, page*PAGE);
        uint8_t *sys = pseudo_file_sys+page*PAGE;
        flash_page_program(sys, page);
        INFO_PAGES--;
        page++;
    }
    Serial.printf("done.\r\n");
    Serial.println("");
}

bool extract(void)
{
  
    unsigned char buf2[PAGE];
 
    uint8_t tmp_page = 0;  
    Serial.println("");
    Serial.println("Extracting file info:");
    // file number?
    flash_read_pages(buf2, tmp_page, 1);
    uint16_t files_nr = ((uint16_t)(buf2[0]) << 0x8) + buf2[1];          // extract file #
    uint8_t  page_offset = 0x1 + ((files_nr*INFO_SLOT_SIZE + 0x2) >> 0x8); // page offset
   
    if (!files_nr || files_nr > MAX_FILES))  { 
              
              Serial.println("-->  no files found"); 
              return false; 
    }
    else Serial.printf("-- > found %d file(s): ok \r\n", files_nr); 
    
    // ... ok, in that case, read file info:
    unsigned char tmp_buf[PAGE*page_offset];
    flash_read_pages(tmp_buf, tmp_page, page_offset);
    get_INFO_PAGES(tmp_buf, files_nr); 
  
    Serial.println("done.");
    return true;
}
    
void erase(void) {
    Serial.println("Erasing chip....");
    flash_chip_erase(true);
    Serial.println("done.");
}

void get_INFO_PAGES(uint8_t *fileinfo, uint8_t _files) {
  
     fileinfo += 0x2; // skip file #
     uint8_t _f = _files;
     while (_f) {
         uint32_t tmp, adr;
         tmp = *fileinfo; fileinfo++; // 1
         adr  = tmp << 24; 
         tmp = *fileinfo; fileinfo++; // 2
         adr += tmp << 16;
         tmp = *fileinfo; fileinfo++; // 3
         adr += tmp << 8;
         tmp = *fileinfo; fileinfo++; // 4
         adr += tmp;
         file_adr[_files-_f] = adr;   // file pos
         uint8_t len = MAX_LEN; // names
         char _x;
         while (len) {
               _x = recovered_names[_files-_f][MAX_LEN-len] = *fileinfo;  
               Serial.print(_x); 
               len--;   
               fileinfo++;  
         };  // get name
         
         Serial.printf(" -- > file_adr[%d] = 0x%07X ......\r\n", _files - _f, adr);
        _f--;
     }
     
  Serial.println("");
};

char makenice(char _n, uint8_t _ext) {
   
   if (_ext) return ' ';
   char tmp = _n;   
   if (tmp == '.') _EXT = 1; 
   if (tmp >= 'A' && tmp  <= 'Z' ) tmp = tmp + 'a' - 'A';
   else tmp = ' ';
   return tmp;
}


void setup()
{
  SPI.setMOSI(7);
  SPI.setSCK(14);
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {;}  
  delay(1000); 
  Serial.print("\r\n\r\nW25Q128FV Serial Flasher \r\nInitializing SD card...");
  if (!SD.begin(10)) {
    Serial.println("failed!");
    return;
  }  
  Serial.println("done.\r\n");
  dir = SD.open(DIRECTORY);
  fsize = 0;
  fcnt = 0;
  while(1) 
  {
    entry = dir.openNextFile();
    if (!entry || fcnt >= MAX_FILES) break;    
    int s = entry.size();   
    if ( (s & 0xff) > 0) s |= 0xff;
    filename[fcnt] = entry.name(); // --> filenames
    Serial.printf("%s\t\t\t%d bytes\r\n", filename[fcnt].c_str(), s);
    fsize += s;   
    entry.close();  
    fcnt++;
  } 
  dir.close();
  fcnt_all = fcnt;
  
  Serial.printf("\t\t\t%d file(s): %d bytes \r\n", fcnt, fsize);
  INFO_PAGES = 1 + ((fcnt*INFO_SLOT_SIZE + 0x2) >> 0x8); // == # pages; 12 bytes: pos + name + fcnt;
  Serial.printf("\t\t\tPages consumed for file info: %d page(s)\r\n", INFO_PAGES);
  fsize += INFO_PAGES*PAGE;
  Serial.printf("\t\t\t--> total: %d bytes \r\n", fsize);
  Serial.println("");
  
  if (!fsize) goto ready;
  if (fsize < FLASHSIZE) {
      //flash_init(15);
      flash_init();
      int flashstatus = flash_read_status();
      flash_read_id(id_tab);
      Serial.printf("Flash Status: 0x%X, ID:0x%X,0x%X,0x%X,0x%X ", flashstatus , id_tab[0], id_tab[1], id_tab[2], id_tab[3]);   
      if (id_tab[0]!=0xef || id_tab[1]!=0x40 || id_tab[2]!=0x18 || id_tab[3]!=0x00) {Serial.println(" is not ok."); goto end;}
      Serial.printf(" is ok.\r\nFile(s) fit(s) in serial flash, %d Bytes remaining.\r\n\r\n", FLASHSIZE - fsize);

      Serial.print("Check flash content: \r\n");
      extract();    
      if (verify()) { Serial.println("Flash content ok. Nothing to do."); goto end; }
      erase();
      flash();      
      verify();     
      extract(); 
      
  
end:            
      dir.close();
  }
  else Serial.println("Does not fit.");  
  
ready:  
  Serial.println("Ready.");
}



void loop()
{}



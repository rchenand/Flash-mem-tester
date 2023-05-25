#include <SPI.h>

// Read Device ID
#define RDID 0x9F // Read ID
#define RES_MX66 0xAB // Read JEDEC Serial Flash Parameters 
#define REMS 0x90 //Read electronic manufacturer and device ID
#define QPIID 0xAF // Read Quad ID

// Register Access
#define RDSR 0x05 // Read Status Register (MX66L, MT25)
#define RDSR1 0x05 // Read Status Register 1 (S25FLxxxL, S25FLxxxS Only)
#define RDSR2 0x07 // Read Status Register 2 (S25FLxxxL Only)
#define RDSCUR 0x2B //Read Security Register (MX66L Only)
#define RFSR 0x70 //Read Flag Status Register (MT25 Only)

#define EN4B 0xB7 //Enable 4-byte address mode
#define EX4B 0xE9 //Exit 4-byte address mode


// RDSR1 Bits
#define WIP 0 //Write in Progress
#define WEL 1 //Write Enable Latch
//End RDSR1 Bits
#define RDCR 0x15 // Read Configuration Register
#define RDAR 0x65 // Read Any Register
#define WRR 0x01 // Write Register, All Status and Configuration
#define WRDI 0x04 // Write Disable
#define WREN 0x06 // Write Enable for Non-volatile data change
#define CLSR 0x30 // Clear Status Register

// Read Flash Array
#define READ 0x03 // Read
#define FAST_READ 0x0B // Fast Read

//Program Flash Array
#define PP 0x02 // Page Program
#define PP4 0x38 //Page Program
#define QPP 0x32 //Quad Page Program
#define QPP4 0x34 //Quad Page Program

//Erase Flash Array
#define SE 0x20 //Sector Erase
#define BE 0xD8 //Block Erase
#define CE 0x60 //Chip Erase
#define CE_ALT 0xC7 //Chip Erase (Alternate Instruction)

//Erase/Program Suspend/Resume
#define EPS 0x75 //Erase/Program Suspend
#define EPR 0x7A //Erase/Program Resume

//Reset
#define RSTEN 0x66 //Software Reset Enable
#define RST 0x99 //Software Reset

//Deep Power Down
#define DPD 0xB9 //Deep Power Down

#define WIP 0 //Write in Progress
#define WEL 1 //Write Enable Latch
#define E_ERR 6 //Erase Error Occurred (1 == Error)
#define P_ERR 5 //Programming Error Occurred (1 == Error)
#define E_ERR_MT25 5
#define P_ERR_MT25 4
#define P_ERR_S25HST 6
#define E_ERR_S25HST 5

class NORFlash {
  private:
    pin_size_t cs1;
    pin_size_t cs2;
    pin_size_t cs3;
    pin_size_t reset;
    pin_size_t wp;
    uint8_t last_known_wip;
  public:
    uint8_t wip;
    uint8_t wel;
    uint8_t p_err;
    uint8_t e_err;
    uint8_t mfg_id;
    uint8_t device_type;
    uint8_t density_code;
    uint16_t page_size = 256;
    uint32_t block_size = 65536; // 64 kB (default, only one that currently isn't 64KB is S25HST)
    uint16_t sector_size = 4096; // 4 kB
    uint8_t current_sector = 0;
    uint8_t block_verbose[65536]; // will need to adjust for s25hst
    int error_bytes;
    int bytes_covered = 0;
    String part_number;
    String mfg;
    uint16_t density = 0;
    String mode;
    uint8_t tmp;

    void erase(){
      this->mode = "Erase";
      digitalWrite(this->cs1, LOW);
      SPI.transfer(WREN); //Write Enable (must be enabled before every command)
      digitalWrite(this->cs1, HIGH);
      digitalWrite(this->cs1, LOW); 
      SPI.transfer(CE);
      digitalWrite(this->cs1, HIGH);
    }

    void block_erase(){
      this->mode = "Block Erase";
      digitalWrite(this->cs1,LOW);
      SPI.transfer(WREN); //WREN must be issued to set WEL
      digitalWrite(this->cs1,HIGH);
      digitalWrite(this->cs1, LOW);
      if ((this->mfg_id == 0x01 && this->device_type == 0x02) || (this->mfg_id ==0x34)){ //S25FL-S and S25HST require specifying 4-byte addressing
        SPI.transfer(0xDC);
      }
      else{
      SPI.transfer(BE); //Block erase, followed by address of block to erase
      }
      SPI.transfer(0x00);
      SPI.transfer(0x00);
      SPI.transfer(0x00);
      SPI.transfer(0x00); //Start at address zero
      digitalWrite(this->cs1, HIGH);
    }

    uint8_t read_byte(uint64_t addr){
      digitalWrite(this->cs1, LOW);
      SPI.transfer(READ);
      SPI.transfer((addr >> 16) & 0xFF);
      SPI.transfer((addr >> 8) & 0xFF);
      SPI.transfer(addr & 0xFF);
      uint8_t read_data = SPI.transfer(0x00);
      digitalWrite(this->cs1, HIGH);
      return read_data;
    }

    void read(){
      this->mode = "Read";
      this->error_bytes = 0;
      this->bytes_covered = 0;
      this->current_sector = 0;
      //Read everything out of memory, expect to see 0xAA for each byte, if not then add to error count
      digitalWrite(this->cs1, LOW);
      if ((this->mfg_id == 0x01 && this->device_type == 0x02) || (this->mfg_id ==0x34)){ //enter 4-byte addressing mode specifically
          SPI.transfer(0x13);  
        }
      else{      
      SPI.transfer(READ);
      }
      SPI.transfer(0x00);
      SPI.transfer(0x00);
      SPI.transfer(0x00);
      SPI.transfer(0x00); //Start at address zero
      //Loop for the entire density of the chip, if any byte is not 0xAA then report as an error
      for (int i = 0; i < this->block_size; i++){
        //Check if we are in a new sector
        if(i % this->sector_size == 0){
          this->current_sector++;
        }
        uint8_t data = SPI.transfer(0x00);
        if (data != 0xAA){ 
          this->error_bytes++;
        }
        this->bytes_covered++;
        this->block_verbose[i] = data;
      }
    }
    
    void write_byte(uint8_t save_me, uint32_t addr){
      /*
        Notes on Program Flash Array Section 8.5
          Page Programming allows up to a page size of 256 bytes in one operation
          Before the PP (Page Program) command can be accepted by the device a WREN (Write Enable) command must be issued.
          After recieing the enable the WEL (Write Enable Latch) in the status regsiter is set and operations are enabled.
          Page Program Structure:
          | * * * WREN * * * | * * * PP * * * | * * * adress byte 1 * * * | * * * adress byte 2 * * * | * * * adress byte 3 * * * | * * * data... * * * |
          This assumes one is using the 3-byte addressing mode, 4-byte addressing is also possible and has a different command structure. 
      */
      digitalWrite(this->cs1, LOW);  
      SPI.transfer(WREN); //Write Enable (must be enabled before every command)
      digitalWrite(this->cs1, HIGH);
      digitalWrite(this->cs1, LOW); 
      SPI.transfer(PP);
      SPI.transfer((addr >> 24) & 0xFF);
      SPI.transfer((addr >> 16) & 0xFF);
      SPI.transfer((addr >> 8) & 0xFF);
      SPI.transfer(addr & 0xFF);
      SPI.transfer(save_me);
      digitalWrite(this->cs1, HIGH);
    }

    void write(){
      this->mode = "Write";
      //Write alternating bits to every byte of memory on the chip
      uint8_t remember_me = 0xAA;
      this->bytes_covered = 0;
      this->current_sector = 0;
      //Bytes must be written in batches of 265 bytes (one page)
      for (uint32_t i = 0; i < this->block_size; i+=128){
        //Check if we are in a new sector
        if(i % this->sector_size == 0){ //tracking what sector currently on, can use for debugging
          this->current_sector++; // 16 sectors total in a block for 64 KB sector
        }
        digitalWrite(this->cs1, LOW);  
        SPI.transfer(WREN); //Write Enable (must be enabled before every command)
        digitalWrite(this->cs1, HIGH);
        read_status();
        this->mode = "Write";
        digitalWrite(this->cs1, LOW); 
        if ((this->mfg_id == 0x01 && this->device_type == 0x02) || (this->mfg_id ==0x34)){ //specifically 4 byte addressing
          SPI.transfer(0x12);  
        }
        else{
        SPI.transfer(PP);
        }
        SPI.transfer((i >> 24) & 0xFF);
        SPI.transfer((i >> 16) & 0xFF);
        SPI.transfer((i >> 8) & 0xFF);
        SPI.transfer(i & 0xFF);
        for (int j = 0; j < 256; j++){
          SPI.transfer(remember_me); // 0xAA
          this->bytes_covered++;
        }
        digitalWrite(this->cs1, HIGH);        
      }
    }

    void software_reset(){
      this->mode = "Software Reset";
      digitalWrite(this->cs1, LOW);
      SPI.transfer(RSTEN);
      delay(10);
      SPI.transfer(RST);
      digitalWrite(this->cs1, HIGH);
      enable_four_byte_addr();
    }

    void hardware_reset(){
      this->mode = "Hardware Reset";
      digitalWrite(this->cs1, HIGH);
      delay(1);
      digitalWrite(this->reset, LOW);
      delay(10);
      digitalWrite(this->reset, HIGH);
      delay(1000);
      enable_four_byte_addr();
    }

    void read_id(){
      //Try to read the chip id to determine who we are
      digitalWrite(cs1, LOW);
      SPI.transfer(RDID); // Read Idenfification
      this->mfg_id = SPI.transfer(0x00); // The Manufacturer ID 
      this->device_type = SPI.transfer(0x00); //The device type
      this->density_code = SPI.transfer(0x00); //The device density
      digitalWrite(cs1, HIGH);
      delay(10);

      if (this->mfg_id == 0xC2){
        //This is a Macronix part
        this->mfg = "Macronix";
        //What is the device type?
        switch(this->device_type) {
          case 0x20:
            this->part_number = "MX66L1G45G";
            break;
          default:
            this->part_number = "Unknown";
        }

        //What is the density?
        switch(this->density_code) {
          case 0x1B:
            this->density = 1024;
            break;
          default:
            this->part_number = "Unknown";
        }

      }
      else if (this->mfg_id == 0x01){
        //This is an Infineon part
        this->mfg = "Infineon";
        //What is the device type
        switch(this->device_type) {
          case 0x60:
            this->part_number = "S25FL";
            break;
          case 0x02:
            this->part_number = "S25FL";
            break;
          default:
            this->part_number = "Unknown";
        }

        //What is the density?
        switch(this->density_code) {
          case 0x18:
            this->part_number += "128";
            this->density = 128;
            break;
          case 0x19:
            this->part_number += "256";
            this->density = 256;
            break;
          default:
            this->part_number = "Unknown";
        }
        switch(this->device_type) {
          // is this S25HLxxxL or S25HLxxxS? 
          case 0x60:
            this->part_number += "L";
            break;
          case 0x02:
            this->part_number += "S";
            break;
          default:
            this->part_number = "Unknown";
        }       
      }
      else if (this->mfg_id == 0x20){
        //This is a Micron part
        this->mfg = "Micron";
        //What is the device type?
        switch(this->device_type) {
          case 0xBA:
            this->part_number = "MT25QL";
            break;
          case 0xBB:
            this->part_number = "MT25QU";
            break;
          default:
            this->part_number = "Unknown";
        }

        //What is the density?
        switch(this->density_code) {
          case 0x17:
            this->density = 64;
            this->part_number += "64M";
            break;
          case 0x18:
            this->density = 128;
            this->part_number += "128M";
            break;
          case 0x19:
            this->density = 256;
            this->part_number += "256M";
            break;
          case 0x20:
            this->density = 512;
            this->part_number += "512M";
            break;
          case 0x21:
            this->density = 1024;
            this->part_number += "1G";
            break;
          case 0x22:
            this->density = 2048;
            this->part_number += "2G";
            break;
          default:
            this->part_number = "Unknown";
        }
      }
      else if (this->mfg_id == 0x34){
        //This is a Infineon Cypress 1GB S25HSxxxT part
        this->mfg = "Cypress";
        //What is the device type?
        this->block_size = 262144; //256 kB block, 32 4kb sectors 
        switch(this->device_type) {
          case 0x2B:
            this->part_number = "S25HS01GT";
            break;
          default:
            this->part_number = "Unknown";
        }

        //What is the density?
        switch(this->density_code) {
          case 0x1B:
            this->density = 1024; // 1 GB, 1024 MB
            break;
          default:
            this->part_number = "Unknown";
        }
        digitalWrite(this->cs1,LOW); //configure configuration register 3 for uniform 256kb sectors, S25HST only
        SPI.transfer(WREN); //WREN must be issued to set WEL
        digitalWrite(this->cs1,HIGH);
        digitalWrite(this->cs1, LOW);
        SPI.transfer(0x01); //write register
        SPI.transfer(0x00); //status register
        SPI.transfer(0x00); 
        SPI.transfer(0x08); // config 3
        SPI.transfer(0x08); 
        SPI.transfer(0x08);
        digitalWrite(this->cs1,HIGH);    

      }
    }

    void read_status(){
      /*
        Design for Reliability Note:
          From page 73 of the datasheet:
            'The host system can determine when a write, program, erase, suspend or other embedded operation is complete
            by monitoring the Write in Progress (WIP) bit in the Status Register.'
          Get the WIP bit from the read status register 1. Read from status register 2 to get the state of the program error (P_ERR) and erase error (E_ERR).
          Note that the WIP bit will remain 1 when the P_ERR or E_ERR bits are 1 and the device will remain busy. This can be cleared with the Software Reset, 
          Clear Staus Register or Hardware Reset pin.
      */
      if (this->mfg_id == 0xC2){
        //This is a Macronix part
        digitalWrite(cs1, LOW);
        SPI.transfer(RDSR);
        uint8_t status1 = SPI.transfer(0x00);
        digitalWrite(cs1, HIGH);
        this->wip = (status1 >> WIP) & 0x1;
        this->wel = (status1 >> WEL) & 0x1;
        digitalWrite(cs1, LOW);
        SPI.transfer(RDSCUR);
        uint8_t status2 = SPI.transfer(0x00);
        digitalWrite(cs1, HIGH);
        this->p_err = (status2 >> P_ERR) & 0x1;
        this->e_err = (status2 >> E_ERR) & 0x1;
      }
      else if (this->mfg_id == 0x01 && this->device_type == 0x02){
        //This is an Infineon part S25FL256S
        digitalWrite(cs1, LOW);
        SPI.transfer(RDSR1);
        uint8_t status1 = SPI.transfer(0x00); //Dummy Cycles to get info from SR1NV
        digitalWrite(cs1, HIGH);
        // WIP, WEL and Erase and Program errors all in Status Register 1
        this->wip = (status1 >> WIP) & 0x1;
        this->wel = (status1 >> WEL) & 0x1;
        this->e_err = (status1 >> 5) & 0x1; 
        this->p_err = (status1 >> 6) & 0x1;
      }
      
      else if (this->mfg_id == 0x01 && this->device_type == 0x60){
        //This is an Infineon part S25FL256L
        digitalWrite(cs1, LOW);
        SPI.transfer(RDSR1);
        uint8_t status1 = SPI.transfer(0x00); //Dummy Cycles to get info from SR1NV
        digitalWrite(cs1, HIGH);
        this->wip = (status1 >> WIP) & 0x1;
        this->wel = (status1 >> WEL) & 0x1;
        digitalWrite(cs1, LOW);
        SPI.transfer(RDSR2);
        uint8_t status2 = SPI.transfer(0x00);
        digitalWrite(cs1, HIGH);
        this->p_err = (status2 >> P_ERR) & 0x1;
        this->e_err = (status2 >> E_ERR) & 0x1;
      }
      else if (this->mfg_id == 0x20){
        //This is a Micron part
        digitalWrite(cs1, LOW);
        SPI.transfer(RDSR);
        uint8_t status1 = SPI.transfer(0x00);
        digitalWrite(cs1, HIGH);
        this->wip = (status1 >> WIP) & 0x1;
        this->wel = (status1 >> WEL) & 0x1;
        digitalWrite(cs1, LOW);
        SPI.transfer(RFSR);
        uint8_t status2 = SPI.transfer(0x00);
        digitalWrite(cs1, HIGH);
        this->p_err = (status2 >> P_ERR_MT25) & 0x1;
        this->e_err = (status2 >> E_ERR_MT25) & 0x1;
      }
      else if (this->mfg_id == 0x34){
        //This is a Infineon Cypress part
        digitalWrite(cs1, LOW);
        SPI.transfer(RDSR1);
        uint8_t status1 = SPI.transfer(0x00);
        digitalWrite(cs1, HIGH);
        // RDYBSY (WIP), E_ERR and P_ERR all in Status Register 1 
        this->wip = (status1 >> WIP) & 0x1; //RDYBSY (device ready/busy status flag)
        this->wel = (status1 >> WEL) & 0x1;
        this->e_err = (status1 >> E_ERR_S25HST) & 0x1;
        this->p_err = (status1 >> P_ERR_S25HST) & 0x1; 
      }
      if (this->wip == 0 && (this->mode == "Erase" || this->mode == "Write")){
        this->mode = "Standby";
      }

    }

    void enable_four_byte_addr(){
      //Enable 4-byte addressing mode
      digitalWrite(cs1, LOW);
      SPI.transfer(EN4B);
      digitalWrite(cs1, HIGH);
    }

    NORFlash(pin_size_t sck, pin_size_t sdi, pin_size_t sdo, pin_size_t reset, pin_size_t cs1, pin_size_t cs2, pin_size_t cs3){
      //write protect is already disabled with the HW
      this->reset = reset;     
      this->cs1 = cs1; 
      this->cs2 = cs2;
      this->cs3 = cs3;
      this->wp = wp;
      //Begin the SPI Interface for comms with the NOR Flash
      SPI.begin();
      //Change SPI setting per datasheet, page 17
      //Change the SPI Settings to allow Clock Frequency of 10MHz (absolute max per datasheet is 133MHz)
      //Most Significant Bit First
      //Data Mode 0 (Clock Polarity = 0 and Clock Phase Angle = 0)
      SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); // see if can actually or need to decrease freq 
      SPI.setCS(cs1);  
      SPI.setSCK(sck);
      SPI.setRX(sdi);
      SPI.setTX(sdo);
      pinMode(cs1, OUTPUT);
      pinMode(cs2, OUTPUT);
      digitalWrite(cs2, HIGH);
      pinMode(cs3, OUTPUT);
      digitalWrite(cs3, HIGH);
      //Hold RESET low (Pulse high for reset)
      pinMode(reset, OUTPUT);
      digitalWrite(reset, HIGH);  
      this->mode = "Standby";
      read_id();
      //Enable 4-byte addressing mode
      if(this->mfg_id != 0x01 && this->device_type != 0x02){ //S25FLS doesn't have this feature 
        enable_four_byte_addr();
      }      
    }
};

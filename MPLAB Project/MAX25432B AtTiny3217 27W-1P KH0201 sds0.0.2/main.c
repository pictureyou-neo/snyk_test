/*
    (c) 2018 Microchip Technology Inc. and its subsidiaries. 
    
    Subject to your compliance with these terms, you may use Microchip software and any 
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party 
    license terms applicable to your use of third party software (including open source software) that 
    may accompany Microchip software.
    
    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER 
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY 
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS 
    FOR A PARTICULAR PURPOSE.
    
    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP 
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO 
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL 
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT 
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS 
    SOFTWARE.
*/

// TCPM version 6-28-21

#include "mcc_generated_files/mcc.h"
#include "mx_tcpm.h"
#include "tcpci_deps.h"
#include <avr/wdt.h>

#define LED_DCP_MODE() IO_CLED_SetHigh(); IO_DLED_SetLow();
#define LED_CDP_MODE() IO_CLED_SetLow(); IO_DLED_SetHigh();
#define _ON 0x01
#define _OFF 0x00
#define M_SW_DELAY  100
#define _EEPROM_ADDR    0x10


extern char log_arr[10][128];
extern uint8_t log_index;
extern uint16_t AUTO_CDP_DCP_MODE;

uint16_t pre_sw = _OFF, now_sw = _OFF;
uint16_t now_eep = _OFF;
static uint16_t sw_cnt = 0, eep_cnt = 0;

void print_log()
{
  for (int i=0; i<log_index; i++)
    printf("%s",log_arr[i]);
  log_index = 0;
}

// Implement a 1 millisecond timer for tcpm
void user_periodic_timer_ms_interrupt(void)
{
    mx_tcpm_increment_ms_clk();
    
    if (mx_tcpm_get_ms_clk() % 1000 == 0) {
        // Placeholder for debug second pulse 
    }
}

void enable_interrupts();
void disable_interrupts();
uint8_t int_en = 0;

// Interrupt handler for ALERTB
// Do not allow nested interrupts
void alertb_handler()
{
    if (int_en == 0)
        return;
    disable_interrupts();
    //mx_tcpm_irq();
    while (IO_PC2_GetValue() == 0)
        mx_tcpm_irq();
    enable_interrupts();
}

void enable_interrupts() 
{
    int_en = 1;
}

void disable_interrupts()
{
    int_en = 0;
}

// I2C functions. Do not allow interrupts to occur during i2c operations
int reg_read_bytes(const uint8_t i2c_device_addr, const uint8_t reg, uint8_t vals[], const uint8_t len)
{
    if (int_en) {
        disable_interrupts();
        I2C0_example_readDataBlock(i2c_device_addr, reg, vals, len);
        enable_interrupts();
    }
    else {
        I2C0_example_readDataBlock(i2c_device_addr, reg, vals, len);
    }
    return 0;
}

int reg_write_bytes(const uint8_t i2c_device_addr, const uint8_t reg, const uint8_t vals[], const uint8_t len)
{
    if (int_en) {
        disable_interrupts();
        I2C0_example_writeDataBlock(i2c_device_addr, reg, vals, len);
        enable_interrupts();
    }
    else {
        I2C0_example_writeDataBlock(i2c_device_addr, reg, vals, len);
    }
    return 0;
}

// Callbacks for mx_tcpm
const mx_tcpm_callbacks user_callbacks =
{
  reg_read_bytes,
  reg_write_bytes
};



void m_switch_check(void)
{
    /* active low : M_SW == HIGH : not contact / M_SW == LOW : contact */
    if(IO_M_SW_GetValue() == 0x00) { now_sw = _ON; }
    else { now_sw = _OFF; }
    
    if(now_sw == _ON)
    {
      sw_cnt++;
      /* CDP - DCP Mode Change */
      if(sw_cnt >= M_SW_DELAY)
      {
          sw_cnt = M_SW_DELAY;
          if(pre_sw == _OFF)
          {
              pre_sw = _ON;
              
              if(now_eep == 0xA0) {
                  /* DCP -> CDP Mode change */
                  AUTO_CDP_DCP_MODE = 0b01;
                  now_eep = 0x0A;
                  LED_CDP_MODE();
              }
              else {
                  /* CDP -> DCP Mode change */
                  AUTO_CDP_DCP_MODE = 0b10;
                  now_eep = 0xA0;
                  LED_DCP_MODE();
              }
              
              eep_cnt = 0;
              while (FLASH_WriteEepromByte(_EEPROM_ADDR, now_eep))
              {
                  eep_cnt++;
                  if(eep_cnt >= 50)
                  {
                      eep_cnt = 0;
                      break;
                  }
              }
              disable_interrupts();
              mx_tcpm_reinit();
              enable_interrupts();
          }
       }
    }
    
    if(now_sw == _OFF)
    {
        sw_cnt = 0;
        eep_cnt = 0;
        pre_sw = _OFF;
    }
    
}
void m_sw_init(void)
{
    now_eep = FLASH_ReadEepromByte(_EEPROM_ADDR);

    if(now_eep == 0xA0) {
        /* CDP -> DCP Mode change */
        AUTO_CDP_DCP_MODE = 0b10;
        now_eep = 0xA0;
        LED_DCP_MODE();
    }
    else {
        /* DCP -> CDP Mode change */
        AUTO_CDP_DCP_MODE = 0b01;
        now_eep = 0x0A;
        LED_CDP_MODE();
    }
    
}

void wdt_clear(void)
{
    wdt_reset();
}


/*
    Main application
*/
int main(void)
{
    /* Initializes MCU, drivers and middleware */
    SYSTEM_Initialize();
    

    RTC_SetPITIsrCallback(user_periodic_timer_ms_interrupt);

    RTC_EnablePITInterrupt();
    
    // Configure the TCPM for number of ports and device addresses for each port
    mx_tcpm_set_port_addr(0x50);
    
    // first initialize
    m_sw_init();
            
    // Initialize the tcpm with i2c callback struct
    mx_tcpm_init(user_callbacks);

    // Interrupt handler for alertb
    PORTC_IO_PC2_SetInterruptHandler(alertb_handler);
    enable_interrupts();
    
    while (1){
        
        // Secondary check for alertb low
        if (IO_PC2_GetValue() == 0) {
            alertb_handler();
        }
  
        mx_tcpm_work();
        m_switch_check();
        wdt_clear();
    }
}
/**
    End of File
*/
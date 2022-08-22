#ifndef ADB_h
#define ADB_h

#include <Arduino.h>

#include "dwt.h"

#define us_to_cycles(cycles) (uint32_t)((uint64_t)(cycles) * SystemCoreClock / 1000000ul)
#define delay_microseconds(us) do { DWT->CYCCNT = 0; while (DWT->CYCCNT < us_to_cycles(us)); } while(0)

#define ADB_DATA_PIN        PB13
#define ADB_DATA_PORT       GPIOB
#define ADB_DATA_PIN_NO     13
#define ADB_WRITE(bit)      gpio_write_bit(ADB_DATA_PORT, ADB_DATA_PIN_NO, (bit))
#define ADB_READ()          (0!=gpio_read_bit(ADB_DATA_PORT, ADB_DATA_PIN_NO))

#define ADB_ADDRESS(addr)   (addr << 4)
#define ADB_REGISTER(reg)   (reg)
#define ADB_CMD_TALK        (0b11 << 2)
#define ADB_CMD_LISTEN      (0b10 << 2)
#define ADB_CMD_FLUSH       (0b01 << 2)

#define ADB_ADDR_KEYBOARD   2
#define ADB_ADDR_MOUSE      3

#define ADB_BIT_ERROR       0xFF

// Reset: signal low for 3 ms.
static void adb_reset() {
    ADB_WRITE(LOW);
    delay_microseconds(3000);
    ADB_WRITE(HIGH);
}

// Attention: signal low for 800 μs.
static void adb_attention() {
    ADB_WRITE(LOW);
    delay_microseconds(800);
    ADB_WRITE(HIGH);
}

// Sync: signal high for 70 μs.
static void adb_sync() {
    ADB_WRITE(HIGH);
    delay_microseconds(70);
    ADB_WRITE(LOW);
}

// Send a single on ADB_DATA_PIN.
// '0' if bit == 0x0: write 65 μs LOW, 35 μs HIGH.
// '1' if bit >= 0x1: write 35 μs LOW, 65 μs HIGH.
static void adb_write_bit(uint16_t bit) {
    if (bit) { // '1' bit
        ADB_WRITE(LOW);
        delay_microseconds(35);
        
        ADB_WRITE(HIGH);
        delay_microseconds(65);
    }
    else { // '0' bit
        ADB_WRITE(LOW);
        delay_microseconds(65);
        
        ADB_WRITE(HIGH);
        delay_microseconds(35);
    }
}

// Send `length` least significant bits from `bits`,
// starting with the most significant bit.
static void adb_write_bits(uint16_t bits, uint8_t length) {
    uint16_t mask = 1 << (length - 1);
    while (mask) {
        adb_write_bit(bits & mask);
        mask >>= 1;
    }
}

// Like adb_write_bits, but add start and stop bits
static void adb_write_data_packet(uint16_t bits, uint8_t length) {
    adb_write_bit(1);
    adb_write_bits(bits, length);
    adb_write_bit(0);
}

// Send the '0' stop bit, and listen for an SRQ.
// ``If a device in need of service issues a service request,
// it must do so within the 65 μs of the Stop Bit’s low time
// and maintain the line low for a total of 300 μs.''
// Returns true if there was an SRQ.
static bool adb_stop_bit_srq_listen() {
    adb_write_bit(0);
    // TODO: Properly handle SRQ, currently waits it out:
    DWT->CYCCNT = 0;
    while (ADB_READ() == LOW && DWT->CYCCNT < us_to_cycles(300 - 35)) ; 
    return false;
}

// Write a command to the bus.
void adb_write_command(uint8_t command_byte) {
    adb_attention();
    adb_sync();
    adb_write_bits((uint16_t)command_byte, 8);
    adb_stop_bit_srq_listen(); // TODO: Handle the SRQ
}

// Stop-to-start time: period of 140 - 260 μs before device's
// response when the bus is held high.
// Returns: true if the response is starting, false if timeout
static bool adb_wait_tlt(bool response_expected) {
    ADB_WRITE(HIGH);
    delay_microseconds(140);
    uint8_t i = 0;

    DWT->CYCCNT = 0;    
    while (ADB_READ() == HIGH && DWT->CYCCNT < us_to_cycles(260) && response_expected) ;
    
    return true;
}

// Read a single bit from the bus.
static uint8_t adb_read_bit() {
    DWT->CYCCNT = 0;
    
    while (ADB_READ() == LOW) {
        // devices need to stick to 30% precision, 65 * 1.3 = 85 μs
        // if this time is exceeded assume timeout
        if (DWT->CYCCNT > us_to_cycles(85))
            return ADB_BIT_ERROR;
    }
    auto low_time = DWT->CYCCNT;

    DWT->CYCCNT = 0;

    while (ADB_READ() == HIGH) {
        // devices need to stick to 30% precision, 65 * 1.3 = 85 μs
        // if this time is exceeded assume timeout
        if (DWT->CYCCNT > us_to_cycles(85))
            return ADB_BIT_ERROR;
    }
    auto high_time = DWT->CYCCNT;
    
    return (low_time < high_time) ? 0x1 : 0x0;
}

// Read `length` bits from the bus into `buffer`.
bool adb_read_data_packet(uint16_t* buffer, uint8_t length)
{   
    if (adb_read_bit() != 0x1) { // start bit should equal to '1'
        return false;
    }

    *buffer = 0;
    for (uint8_t i = 0; i < length; i++)
    {
        uint8_t current_bit = adb_read_bit();
        if (current_bit == ADB_BIT_ERROR) {
            return false;
        }
        *buffer <<= 1;
        *buffer |= current_bit;
    }

    /* uint8_t stop_bit = */ adb_read_bit(); // should equal to '0'
    return true;
}

void adb_init() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= 1;

    pinMode(ADB_DATA_PIN, OUTPUT_OPEN_DRAIN);
    ADB_WRITE(HIGH);

    while (ADB_READ() == LOW); // wait for the bus

    adb_reset();
}

#endif


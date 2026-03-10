/*
 * This file is part of arduino-pn532-srix.
 * @author Lilz
 * @license GNU Lesser General Public License v3.0
 * Refactored by Senape3000, Fixed by andreaguidi73
 */

#ifndef PN532_SRIX_H
#define PN532_SRIX_H

#include <Arduino.h>
#include <Wire.h>

// DEBUG SETTINGS (uncomment to enable)
// #define PN532_SRIX_DEBUG
#ifdef PN532_SRIX_DEBUG
    #define PN532_DEBUG_PRINT Serial
#endif

// PN532 COMMANDS (no external dependency)
#define PN532_COMMAND_GETFIRMWAREVERSION    (0x02)
#define PN532_COMMAND_SAMCONFIGURATION      (0x14)
#define PN532_COMMAND_RFCONFIGURATION       (0x32)
#define PN532_COMMAND_INLISTPASSIVETARGET   (0x4A)
#define PN532_COMMAND_INCOMMUNICATETHRU     (0x42)

// PN532 I2C PROTOCOL CONSTANTS
#define PN532_I2C_ADDRESS   (0x48 >> 1)
#define PN532_PREAMBLE      (0x00)
#define PN532_STARTCODE1    (0x00)
#define PN532_STARTCODE2    (0xFF)
#define PN532_POSTAMBLE     (0x00)
#define PN532_HOSTTOPN532   (0xD4)
#define PN532_PN532TOHOST   (0xD5)

// PN532 RESPONSE CODES
#define PN532_RESPONSE_OK           (0x00)  // Success
#define PN532_RESPONSE_TIMEOUT      (0x01)  // Timeout (EXPECTED for SRIX4K WRITE!)
#define PN532_RESPONSE_CRC_ERROR    (0x02)
#define PN532_RESPONSE_PARITY_ERROR (0x03)
#define PN532_RESPONSE_COLLISION    (0x06)
#define PN532_RESPONSE_RF_FIELD_OFF (0x0A)
#define PN532_RESPONSE_RF_PROTOCOL  (0x0B)

// SRIX4K COMMANDS
#define SRIX4K_INITIATE     (0x06)
#define SRIX4K_SELECT       (0x0E)
#define SRIX4K_READBLOCK    (0x08)
#define SRIX4K_WRITEBLOCK   (0x09)
#define SRIX4K_GETUID       (0x0B)

// SRIX4K TIMING CONSTANTS - Increased for better compatibility
#define SRIX4K_WRITE_TIME_MS                 (10)    // EEPROM programming time
#define SRIX4K_WRITE_COMMAND_TIMEOUT_MS      (200)   // Increased from 150
#define SRIX4K_WRITE_BUFFER_CLEAR_TIMEOUT_MS (150)   // Increased from 100
#define SRIX4K_WRITE_MAX_RETRIES             (3)

// NEW: Timing for INITIATE/SELECT - Critical for tag compatibility
#define SRIX4K_INITIATE_TIMEOUT_MS           (250)   // Timeout for INITIATE command
#define SRIX4K_SELECT_TIMEOUT_MS             (250)   // Timeout for SELECT command
#define SRIX4K_INITIATE_RETRIES              (5)     // Retry attempts for INITIATE
#define SRIX4K_SELECT_RETRIES                (3)     // Retry attempts for SELECT
#define SRIX4K_READ_RETRIES                  (3)     // Retry attempts for READ_BLOCK
#define SRIX4K_UID_RETRIES                   (3)     // Retry attempts for GET_UID
#define SRIX4K_COMMAND_DELAY_MS              (15)    // Delay between commands
#define SRIX4K_RETRY_DELAY_MS                (20)    // Delay between retries
#define SRIX4K_RF_SETTLE_DELAY_MS            (5)     // RF field settle time

// NEW: Alternative INITIATE command for problematic tags
#define SRIX4K_PCALL16                       (0x06)  // PCALL16 command (same opcode, different data)
#define SRIX4K_PCALL16_DATA                  (0x04)  // Data byte for PCALL16 (vs 0x00 for INITIATE)
#define SRIX4K_INVALID_CHIP_ID               (0x00)  // Invalid chip ID value

class Arduino_PN532_SRIX {
public:
    Arduino_PN532_SRIX(uint8_t irq, uint8_t reset);
    Arduino_PN532_SRIX();

    bool init();
    uint32_t getFirmwareVersion();
    bool setPassiveActivationRetries(uint8_t maxRetries);

    bool SRIX_init();
    bool SRIX_initiate_select();
    bool SRIX_read_block(uint8_t address, uint8_t *block);
    bool SRIX_write_block(uint8_t address, uint8_t *block);
    bool SRIX_write_block_no_verify(uint8_t address, uint8_t *block);
    bool SRIX_get_uid(uint8_t *buffer);

private:
    uint8_t _irq, _reset;
    uint8_t _packetbuffer[64];

    bool SAMConfig();
    void readData(uint8_t *buffer, uint8_t n);
    bool readACK();
    bool isReady();
    bool waitReady(uint16_t timeout);
    void writeCommand(uint8_t *command, uint8_t commandLength);
    bool sendCommandCheckAck(uint8_t *command, uint8_t commandLength, uint16_t timeout = 100);
};

#endif

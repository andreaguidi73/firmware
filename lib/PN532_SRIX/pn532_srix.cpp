/*
 * This file is part of arduino-pn532-srix.
 * @author Lilz
 * @license GNU Lesser General Public License v3.0
 * Refactored by Senape3000, Fixed by andreaguidi73
 */

#include "pn532_srix.h"
#include <string.h> // For memcmp

// Static ACK pattern
static const uint8_t pn532ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static const uint8_t pn532response_firmwarevers[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5};

// Debug macros
#ifdef PN532_SRIX_DEBUG
    #define DEBUG_PRINT(...) PN532_DEBUG_PRINT.print(__VA_ARGS__)
    #define DEBUG_PRINTLN(...) PN532_DEBUG_PRINT.println(__VA_ARGS__)
    #define DEBUG_PRINTHEX(x) PN532_DEBUG_PRINT.print(x, HEX)
#else
    #define DEBUG_PRINT(...)
    #define DEBUG_PRINTLN(...)
    #define DEBUG_PRINTHEX(x)
#endif

// ========== CONSTRUCTORS ==========

Arduino_PN532_SRIX::Arduino_PN532_SRIX(uint8_t irq, uint8_t reset) : _irq(irq), _reset(reset) {
    if (_irq != 255) pinMode(_irq, INPUT);
    if (_reset != 255) pinMode(_reset, OUTPUT);
}

Arduino_PN532_SRIX::Arduino_PN532_SRIX() : _irq(255), _reset(255) {}

// ========== INITIALIZATION ==========

bool Arduino_PN532_SRIX::init() {
    // Note: Wire.begin() must be called BEFORE creating this object

    // Hardware reset if available
    if (_reset != 255) {
        digitalWrite(_reset, HIGH);
        digitalWrite(_reset, LOW);
        delay(400);
        digitalWrite(_reset, HIGH);
        delay(10);
    }

    return SAMConfig();
}

bool Arduino_PN532_SRIX::SAMConfig() {
    _packetbuffer[0] = PN532_COMMAND_SAMCONFIGURATION;
    _packetbuffer[1] = 0x01; // Normal mode
    _packetbuffer[2] = 0x14; // Timeout 50ms * 20 = 1s
    _packetbuffer[3] = 0x01; // Use IRQ pin

    if (!sendCommandCheckAck(_packetbuffer, 4)) { return false; }

    readData(_packetbuffer, 8);
    return (_packetbuffer[6] == 0x15);
}

// ========== LOW-LEVEL I2C FUNCTIONS ==========

void Arduino_PN532_SRIX::readData(uint8_t *buffer, uint8_t n) {
    delay(2);
    Wire.requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)(n + 1));

    // Discard RDY byte
    Wire.read();

    // Read data
    for (uint8_t i = 0; i < n; i++) {
        delay(1);
        buffer[i] = Wire.read();
    }
}

bool Arduino_PN532_SRIX::readACK() {
    uint8_t ackBuffer[6];
    readData(ackBuffer, 6);
    return (memcmp(ackBuffer, pn532ack, 6) == 0);
}

bool Arduino_PN532_SRIX::isReady() {
    if (_irq == 255) return true; // Polling mode
    return (digitalRead(_irq) == LOW);
}

bool Arduino_PN532_SRIX::waitReady(uint16_t timeout) {
    uint16_t timer = 0;

    while (!isReady()) {
        if (timeout != 0) {
            timer += 10;
            if (timer > timeout) return false;
        }
        delay(10);
    }

    return true;
}

void Arduino_PN532_SRIX::writeCommand(uint8_t *command, uint8_t commandLength) {
    uint8_t checksum;
    commandLength++;

    delay(2); // Wakeup delay

    Wire.beginTransmission(PN532_I2C_ADDRESS);

    checksum = PN532_PREAMBLE + PN532_PREAMBLE + PN532_STARTCODE2;
    Wire.write(PN532_PREAMBLE);
    Wire.write(PN532_STARTCODE1);
    Wire.write(PN532_STARTCODE2);

    Wire.write(commandLength);
    Wire.write(~commandLength + 1);

    Wire.write(PN532_HOSTTOPN532);
    checksum += PN532_HOSTTOPN532;

    for (uint8_t i = 0; i < commandLength - 1; i++) {
        Wire.write(command[i]);
        checksum += command[i];
    }

    Wire.write((uint8_t)~checksum);
    Wire.write(PN532_POSTAMBLE);

    Wire.endTransmission();
}

bool Arduino_PN532_SRIX::sendCommandCheckAck(uint8_t *command, uint8_t commandLength, uint16_t timeout) {
    writeCommand(command, commandLength);

    if (!waitReady(timeout)) return false;

    return readACK();
}

// ========== PN532 GENERIC FUNCTIONS ==========

uint32_t Arduino_PN532_SRIX::getFirmwareVersion() {
    _packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;

    if (!sendCommandCheckAck(_packetbuffer, 1)) { return 0; }

    readData(_packetbuffer, 12);

    if (memcmp(_packetbuffer, pn532response_firmwarevers, 6) != 0) { return 0; }

    uint32_t response = _packetbuffer[7];
    response <<= 8;
    response |= _packetbuffer[8];
    response <<= 8;
    response |= _packetbuffer[9];
    response <<= 8;
    response |= _packetbuffer[10];

    return response;
}

bool Arduino_PN532_SRIX::setPassiveActivationRetries(uint8_t maxRetries) {
    _packetbuffer[0] = PN532_COMMAND_RFCONFIGURATION;
    _packetbuffer[1] = 5;
    _packetbuffer[2] = 0xFF;
    _packetbuffer[3] = 0x01;
    _packetbuffer[4] = maxRetries;

    return sendCommandCheckAck(_packetbuffer, 5);
}

// ========== SRIX FUNCTIONS ==========

bool Arduino_PN532_SRIX::SRIX_init() {
    _packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    _packetbuffer[1] = 0x01;
    _packetbuffer[2] = 0x03;
    _packetbuffer[3] = 0x00;

    return sendCommandCheckAck(_packetbuffer, 4);
}

bool Arduino_PN532_SRIX::SRIX_initiate_select() {
    uint8_t chip_id = 0;
    bool initiate_success = false;
    
    // ==================== INITIATE with retry ====================
    for (uint8_t retry = 0; retry < SRIX4K_INITIATE_RETRIES && !initiate_success; retry++) {
        // Small delay before retry (except first attempt)
        if (retry > 0) {
            delay(SRIX4K_RETRY_DELAY_MS);
        }
        
        // Try standard INITIATE first
        _packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
        _packetbuffer[1] = SRIX4K_INITIATE;
        _packetbuffer[2] = 0x00;  // Standard INITIATE

        bool ack_received = sendCommandCheckAck(_packetbuffer, 3, SRIX4K_INITIATE_TIMEOUT_MS);
        
        // On ACK failure, try PCALL16 variant on odd retry attempts (alternating strategy)
        if (!ack_received && (retry % 2 == 1)) {
            _packetbuffer[2] = SRIX4K_PCALL16_DATA;  // Try PCALL16 variant
            ack_received = sendCommandCheckAck(_packetbuffer, 3, SRIX4K_INITIATE_TIMEOUT_MS);
        }
        
        if (!ack_received) {
            continue;
        }

        // Wait for response
        delay(SRIX4K_RF_SETTLE_DELAY_MS);
        
        // Read response
        readData(_packetbuffer, 10);

        if (_packetbuffer[7] == PN532_RESPONSE_OK) {
            chip_id = _packetbuffer[8];
            initiate_success = true;
        }
        // Also accept timeout with valid chip_id (some tags behave this way)
        else if (_packetbuffer[7] == PN532_RESPONSE_TIMEOUT && _packetbuffer[8] != SRIX4K_INVALID_CHIP_ID) {
            chip_id = _packetbuffer[8];
            initiate_success = true;
        }
    }

    if (!initiate_success) {
#ifdef PN532_SRIX_DEBUG
        PN532_DEBUG_PRINT.println(F("INITIATE failed after all retries"));
#endif
        return false;
    }

    // Delay between INITIATE and SELECT
    delay(SRIX4K_COMMAND_DELAY_MS);

    // ==================== SELECT with retry ====================
    for (uint8_t retry = 0; retry < SRIX4K_SELECT_RETRIES; retry++) {
        if (retry > 0) {
            delay(SRIX4K_RETRY_DELAY_MS);
        }
        
        _packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
        _packetbuffer[1] = SRIX4K_SELECT;
        _packetbuffer[2] = chip_id;

        if (!sendCommandCheckAck(_packetbuffer, 3, SRIX4K_SELECT_TIMEOUT_MS)) {
            continue;
        }

        delay(SRIX4K_RF_SETTLE_DELAY_MS);
        
        readData(_packetbuffer, 10);

        if (_packetbuffer[7] == PN532_RESPONSE_OK) {
#ifdef PN532_SRIX_DEBUG
            PN532_DEBUG_PRINT.print(F("SELECT success, chip_id: 0x"));
            PN532_DEBUG_PRINT.println(chip_id, HEX);
#endif
            return true;
        }
    }

#ifdef PN532_SRIX_DEBUG
    PN532_DEBUG_PRINT.println(F("SELECT failed after all retries"));
#endif
    return false;
}

bool Arduino_PN532_SRIX::SRIX_read_block(uint8_t address, uint8_t *block) {
    for (uint8_t retry = 0; retry < SRIX4K_READ_RETRIES; retry++) {
        if (retry > 0) {
            delay(SRIX4K_RETRY_DELAY_MS);
            // Re-select tag if retry needed
            if (!SRIX_initiate_select()) {
                continue;
            }
        }
        
        _packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
        _packetbuffer[1] = SRIX4K_READBLOCK;
        _packetbuffer[2] = address;

        if (!sendCommandCheckAck(_packetbuffer, 3, SRIX4K_SELECT_TIMEOUT_MS)) {
            continue;
        }

        delay(SRIX4K_RF_SETTLE_DELAY_MS);
        
        readData(_packetbuffer, 13);

        if (_packetbuffer[7] == PN532_RESPONSE_OK) {
            block[0] = _packetbuffer[8];
            block[1] = _packetbuffer[9];
            block[2] = _packetbuffer[10];
            block[3] = _packetbuffer[11];
            return true;
        }
    }

#ifdef PN532_SRIX_DEBUG
    PN532_DEBUG_PRINT.print(F("READ_BLOCK failed for address 0x"));
    PN532_DEBUG_PRINT.println(address, HEX);
#endif
    return false;
}

bool Arduino_PN532_SRIX::SRIX_write_block(uint8_t address, uint8_t *block) {
    // SRIX4K Write - Corrected protocol handling
    // CRITICAL: SRIX4K tags DO NOT respond to WRITE commands!
    // PN532 timeout (0x01) is EXPECTED behavior, not an error.
    
    DEBUG_PRINT("SRIX_write_block: Starting write to block 0x");
    DEBUG_PRINTHEX(address);
    DEBUG_PRINTLN("");
    
    // Step 1: Send WRITE command
    _packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
    _packetbuffer[1] = SRIX4K_WRITEBLOCK;
    _packetbuffer[2] = address;
    _packetbuffer[3] = block[0];
    _packetbuffer[4] = block[1];
    _packetbuffer[5] = block[2];
    _packetbuffer[6] = block[3];
    
    if (!sendCommandCheckAck(_packetbuffer, 7, SRIX4K_WRITE_COMMAND_TIMEOUT_MS)) {
        DEBUG_PRINTLN("SRIX_write_block: ACK failed");
        return false;
    }
    
    DEBUG_PRINTLN("SRIX_write_block: ACK received");
    
    // Step 2: Read PN532 response (clear buffer)
    // CRITICAL: Response will be 0x01 (timeout) which is EXPECTED!
    if (waitReady(SRIX4K_WRITE_BUFFER_CLEAR_TIMEOUT_MS)) {
        readData(_packetbuffer, 10);
        uint8_t response = _packetbuffer[7];
        
        DEBUG_PRINT("SRIX_write_block: PN532 response = 0x");
        DEBUG_PRINTHEX(response);
        DEBUG_PRINTLN("");
        
        // Accept both OK and TIMEOUT as valid responses
        // TIMEOUT is the expected response for SRIX4K!
        if (response != PN532_RESPONSE_OK && response != PN532_RESPONSE_TIMEOUT) {
            DEBUG_PRINTLN("SRIX_write_block: Unexpected RF error");
            return false;  // Real RF error
        }
    }
    
    // Step 3: Wait for EEPROM programming (datasheet: 5ms typical, 10ms max)
    DEBUG_PRINTLN("SRIX_write_block: Waiting for EEPROM programming");
    delay(SRIX4K_WRITE_TIME_MS);
    
    // Step 4: Re-initiate and select tag for next operation
    DEBUG_PRINTLN("SRIX_write_block: Re-selecting tag for next operation");
    bool success = SRIX_initiate_select();
    
    if (success) {
        DEBUG_PRINTLN("SRIX_write_block: SUCCESS - Write completed, tag ready");
    } else {
        DEBUG_PRINTLN("SRIX_write_block: Write successful, but tag re-select failed (next op may need retry)");
    }
    
    return success;
}

bool Arduino_PN532_SRIX::SRIX_write_block_no_verify(uint8_t address, uint8_t *block) {
    // Fast write without verification - for speed-critical applications
    
    DEBUG_PRINTLN("SRIX_write_block_no_verify: Starting fast write");
    
    // Send WRITE command
    _packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
    _packetbuffer[1] = SRIX4K_WRITEBLOCK;
    _packetbuffer[2] = address;
    _packetbuffer[3] = block[0];
    _packetbuffer[4] = block[1];
    _packetbuffer[5] = block[2];
    _packetbuffer[6] = block[3];
    
    // Check ACK from PN532
    if (!sendCommandCheckAck(_packetbuffer, 7, SRIX4K_WRITE_COMMAND_TIMEOUT_MS)) {
        DEBUG_PRINTLN("SRIX_write_block_no_verify: ACK failed");
        return false;
    }
    
    // Read PN532 response to clear buffer (ignore status - timeout is expected)
    if (waitReady(SRIX4K_WRITE_BUFFER_CLEAR_TIMEOUT_MS)) {
        readData(_packetbuffer, 10);
    }
    
    // Wait for EEPROM programming time
    delay(SRIX4K_WRITE_TIME_MS);
    
    // Re-initiate and select tag for next operation
    bool success = SRIX_initiate_select();
    
    if (success) {
        DEBUG_PRINTLN("SRIX_write_block_no_verify: Complete (unverified)");
    } else {
        DEBUG_PRINTLN("SRIX_write_block_no_verify: Re-select failed");
    }
    
    return success;
}

bool Arduino_PN532_SRIX::SRIX_get_uid(uint8_t *buffer) {
    for (uint8_t retry = 0; retry < SRIX4K_UID_RETRIES; retry++) {
        if (retry > 0) {
            delay(SRIX4K_RETRY_DELAY_MS);
        }
        
        _packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
        _packetbuffer[1] = SRIX4K_GETUID;

        if (!sendCommandCheckAck(_packetbuffer, 2, SRIX4K_SELECT_TIMEOUT_MS)) {
            continue;
        }

        delay(SRIX4K_RF_SETTLE_DELAY_MS);
        
        readData(_packetbuffer, 16);

        if (_packetbuffer[7] == PN532_RESPONSE_OK) {
            for (int i = 0; i < 8; i++) {
                buffer[i] = _packetbuffer[8 + i];
            }
            return true;
        }
    }

#ifdef PN532_SRIX_DEBUG
    PN532_DEBUG_PRINT.println(F("GET_UID failed after all retries"));
#endif
    return false;
}

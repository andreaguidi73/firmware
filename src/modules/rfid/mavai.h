/**
 * @file mavai.h
 * @brief MAVAI - Advanced MyKey/SRIX4K NFC Tool v1.0
 * @info Based on SRIX Tool (PR #1983) with complete MyKey functionality
 * @date 2026-01-17
 */

#ifndef __MAVAI_H__
#define __MAVAI_H__

#include "pn532_srix.h"
#include <Arduino.h>
#include <FS.h>

// SRIX4K Memory Layout
#define SRIX4K_BLOCKS 128
#define SRIX4K_BYTES (SRIX4K_BLOCKS * 4)
#define SRIX_BLOCK_LENGTH 4
#define SRIX_UID_LENGTH 8

// Block types for SRIX4K memory layout
#define SRIX_BLOCK_TYPE_SYSTEM     0  // System blocks (0x00-0x04) - read-only
#define SRIX_BLOCK_TYPE_OTP        1  // One-Time Programmable (block 0x05, 0x06)
#define SRIX_BLOCK_TYPE_LOCKABLE   2  // Lockable blocks (0x07-0x0F)
#define SRIX_BLOCK_TYPE_EEPROM     3  // EEPROM data blocks (0x10-0x7F)

// SRIX4K Manufacturer code (first 2 bytes of UID)
#define SRIX_MANUFACTURER_BYTE1    0xD0
#define SRIX_MANUFACTURER_BYTE2    0x02

// MyKey specific blocks
#define MYKEY_BLOCK_OTP 0x06
#define MYKEY_BLOCK_KEYID 0x07
#define MYKEY_BLOCK_PRODDATE 0x08
#define MYKEY_BLOCK_LOCKID 0x05
#define MYKEY_BLOCK_VENDOR1 0x18
#define MYKEY_BLOCK_VENDOR2 0x19
#define MYKEY_BLOCK_VENDOR1_BACKUP 0x1C
#define MYKEY_BLOCK_VENDOR2_BACKUP 0x1D
#define MYKEY_BLOCK_CREDIT1 0x21
#define MYKEY_BLOCK_CREDIT2 0x25
#define MYKEY_BLOCK_PREVCREDIT1 0x23
#define MYKEY_BLOCK_PREVCREDIT2 0x27
#define MYKEY_BLOCK_TRANS_START 0x34
#define MYKEY_BLOCK_TRANS_END 0x3B
#define MYKEY_BLOCK_TRANS_PTR 0x3C
#define MYKEY_TRANS_HISTORY_SIZE 8  // Transaction buffer holds 8 entries (0-7)
#define MYKEY_TRANS_TOTAL_BLOCKS 9  // Total blocks including pointer (0x34-0x3C)

// Reset values for detection
#define MYKEY_BLOCK18_RESET 0x8FCD0F48
#define MYKEY_BLOCK19_RESET 0xC0820007

// Default vendor values for reset (FEDC0123 - factory default vendor)
#define MYKEY_VENDOR_DEFAULT_HIGH 0x0000FEDC
#define MYKEY_VENDOR_DEFAULT_LOW 0x00000123

class MAVAITool {
public:
    enum MAVAI_State {
        IDLE_MODE,
        READ_TAG_MODE,
        WRITE_TAG_MODE,
        READ_UID_MODE,
        PN_INFO_MODE,
        SAVE_MODE,
        LOAD_MODE,
        MYKEY_INFO_MODE
    };

    MAVAITool();
    ~MAVAITool();

    void setup();
    void loop();

private:
    // MyKey functionality - encryption and state
    uint32_t _encryptionKey;
    uint32_t _currentVendor;
    bool _vendorCalculated;
    
    // MyKey - Cryptographic functions
    void encodeDecodeBlock(uint32_t *block);
    void calculateBlockChecksum(uint32_t *block, uint8_t blockNum);
    void calculateEncryptionKey();
    
    // MyKey - Vendor management
    void importVendor(uint32_t vendor);
    void exportVendor(uint32_t *vendor);
    bool isReset();
    
    // MyKey - Credit management
    uint16_t getCurrentCredit();
    uint16_t getPreviousCredit();
    bool addCents(uint16_t cents, uint8_t day, uint8_t month, uint16_t year);
    bool setCents(uint16_t cents, uint8_t day, uint8_t month, uint16_t year);
    
    // MyKey - Security and validation
    bool checkLockID();
    bool isLocked();
    uint8_t getCurrentTransactionOffset();
    
    // MyKey - Key information
    uint32_t getKeyID();
    String getProductionDate();
    uint16_t getDaysSinceProduction();
    
    // MyKey - Utility functions
    uint32_t daysDifference(uint8_t day, uint8_t month, uint16_t year);
    uint32_t daysSince1995(uint8_t day, uint8_t month, uint16_t year);
    uint32_t getDaysElapsed();
    bool resetKey();
    
    // MyKey - UI functions
    void show_mykey_info();
    void add_credit_ui();
    void set_credit_ui();
    void import_vendor_ui();
    void export_vendor_ui();
    void reset_key_ui();
    void view_credit_ui();
    void view_vendor_ui();
    
    // Helper to get block pointer
    uint32_t* getBlockPtr(uint8_t blockNum);
    uint64_t getUidAsUint64();
    uint32_t getUidForEncryption(); // 4-byte UID in little-endian for MyKey
    uint32_t byteSwap32(uint32_t value); // Byte swap 32-bit value
    uint32_t calculateOTP(uint32_t otpBlock); // Calculate OTP from raw block value
    
    // MyKey - Block read/write helpers
    uint32_t readBlockAsUint32(uint8_t blockNum);
    void writeBlockAsUint32(uint8_t blockNum, uint32_t value);
    void writeBlockToMemory(uint8_t blockNum, uint32_t value);
    
    // Helper: Format uint32_t as 8-character uppercase hex string with leading zeros
    String toHex32(uint32_t value);
    
private:
// PN532 for SRIX - uses IRQ/RST if board has them defined
#if defined(PN532_IRQ) && defined(PN532_RF_REST)
    Arduino_PN532_SRIX *nfc;
    bool _has_hardware_pins = true;
#else
    Arduino_PN532_SRIX *nfc;
    bool _has_hardware_pins = false;
#endif

    MAVAI_State current_state;
    bool _tag_read = false;
    bool _screen_drawn = false;
    uint32_t _lastReadTime = 0;

    // RAM storage for 128 blocks (512 bytes)
    uint8_t _dump[SRIX4K_BYTES];
    uint8_t _uid[SRIX_UID_LENGTH];
    bool _dump_valid_from_read = false;
    bool _dump_valid_from_load = false;
    bool _dump_modified = false;
    
    // Block modification tracking (SrixFlag equivalent)
    bool _blockModified[SRIX4K_BLOCKS];
    
    // Block management functions
    void clearAllModifiedFlags();
    void setBlockModified(uint8_t blockNum);
    bool isBlockModified(uint8_t blockNum);
    uint8_t getBlockType(uint8_t blockNum);
    bool isBlockWritable(uint8_t blockNum);
    
    // Manufacturer validation
    bool validateManufacturerCode();
    
    // Selective write function
    uint8_t writeModifiedBlocksToTag();  // Returns number of blocks written
    
    // Modified block write helper (sets flag automatically)
    void modifyBlock(uint8_t blockNum, uint8_t* data);

    void display_banner();
    void select_state();
    void set_state(MAVAI_State state);
    bool waitForTag();
    void delayWithReturn(uint32_t ms);

    // Core SRIX4K operations
    void read_tag();
    void write_tag();
    void read_uid();
    void show_pn_info();
    void show_main_menu();
    void save_file();
    void load_file();
    void load_file_data(FS *fs, String filepath);
};

void MAVAI_Tool();

#endif

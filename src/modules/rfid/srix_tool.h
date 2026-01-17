/**
 * @file srix_tool.h
 * @brief SRIX4K/SRIX512 Reader/Writer Tool v1.3 - FIXED MyKey
 * @author Senape3000
 * @info https://github.com/Senape3000/firmware/blob/main/docs_custom/SRIX/SRIX_Tool_README.md
 * @date 2026-01-01
 */

#ifndef __SRIX_TOOL_H__
#define __SRIX_TOOL_H__

// DEBUG MODE: uncomment for read the write process output from serial
// #define SRIX_DEBUG_WRITE_SIMULATION

#include "pn532_srix.h"
#include <Arduino.h>
#include <FS.h>

class SRIXTool {
public:
    enum SRIX_State {
        IDLE_MODE,
        READ_TAG_MODE,
        WRITE_TAG_MODE,
        READ_UID_MODE,
        PN_INFO_MODE,
        SAVE_MODE,
        LOAD_MODE
    };

    SRIXTool();
    ~SRIXTool();

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
    bool addCents(uint16_t cents, uint8_t day, uint8_t month, uint8_t year);
    bool setCents(uint16_t cents, uint8_t day, uint8_t month, uint8_t year);
    
    // MyKey - Security and validation
    bool checkLockID();
    uint8_t getCurrentTransactionOffset();
    
    // MyKey - Utility functions
    uint16_t daysDifference(uint8_t day, uint8_t month, uint16_t year);
    bool resetKey();
    
    // MyKey - UI functions
    void show_mykey_info();
    void add_credit_ui();
    void set_credit_ui();
    void import_vendor_ui();
    void export_vendor_ui();
    void reset_key_ui();
    
    // Helper to get block pointer
    uint32_t* getBlockPtr(uint8_t blockNum);
    uint64_t getUidAsUint64();
    uint32_t getUidForEncryption(); // 4-byte UID in little-endian for MyKey
    
    // MyKey - Block read/write helpers
    uint32_t readBlockAsUint32(uint8_t blockNum);
    void writeBlockAsUint32(uint8_t blockNum, uint32_t value);
    
private:
// PN532 for SRIX - uses IRQ/RST if board has them defined
// Devices such as T-Embed CC1101 have embedded PN532 that needs IRQ and RST pins
// If using other device, set -DPN532_IRQ=pin_num and -DPN532_RF_REST=pin_num in platformio.ini
#if defined(PN532_IRQ) && defined(PN532_RF_REST)
    Arduino_PN532_SRIX *nfc;
    bool _has_hardware_pins = true;
#else
    Arduino_PN532_SRIX *nfc;
    bool _has_hardware_pins = false;
#endif

    SRIX_State current_state;
    bool _tag_read = false;
    bool _screen_drawn = false;
    uint32_t _lastReadTime = 0;

    // RAM storage for 128 blocks (512 bytes)
    uint8_t _dump[128 * 4];
    uint8_t _uid[8];
    bool _dump_valid_from_read = false;
    bool _dump_valid_from_load = false;

    void display_banner();
    void dump_tag_details();
    void dump_pn_info();

    void select_state();
    void set_state(SRIX_State state);
    bool waitForTag();
    void delayWithReturn(uint32_t ms);

    void read_tag();
    void write_tag();
    void read_uid();
    void show_pn_info();
    void show_main_menu();
    void save_file();
    void load_file();
    void load_file_data(FS *fs, String filepath);
};

void PN532_SRIX();

#endif

/**
 * @file mavai.h
 * @brief MAVAI - Advanced MyKey/SRIX4K NFC Tool v1.0
 * @date 2026-01-17
 */

#ifndef __MAVAI_H__
#define __MAVAI_H__

#include "pn532_srix.h"
#include <Arduino.h>
#include <FS.h>

// SRIX4K Memory Layout
#define SRIX4K_BLOCKS       128
#define SRIX4K_BYTES        (SRIX4K_BLOCKS * 4)  // 512 bytes
#define SRIX_BLOCK_LENGTH   4
#define SRIX_UID_LENGTH     8

// Block type constants
#define SRIX_BLOCK_TYPE_RESETTABLE_OTP  0
#define SRIX_BLOCK_TYPE_COUNTER         1
#define SRIX_BLOCK_TYPE_LOCKABLE        2
#define SRIX_BLOCK_TYPE_EEPROM          3

// MyKey block addresses
#define MYKEY_BLOCK_OTP         0x06
#define MYKEY_BLOCK_VENDOR1     0x12  // Block 18
#define MYKEY_BLOCK_VENDOR2     0x13  // Block 19
#define MYKEY_BLOCK_CREDIT1     0x15  // Block 21
#define MYKEY_BLOCK_CREDIT2     0x16  // Block 22
#define MYKEY_BLOCK_PREVCREDIT1 0x17  // Block 23
#define MYKEY_BLOCK_PREVCREDIT2 0x18  // Block 24

// Manufacturer code
#define SRIX_MANUFACTURER_BYTE_MSB  0xD0
#define SRIX_MANUFACTURER_BYTE_LSB  0x02

// Block flags structure (MIKAI compatible)
struct SrixBlockFlags {
    uint32_t memory[4]; // 128 bits = 128 blocks
};

// MAVAI states
enum MAVAI_State {
    IDLE_MODE = 0,
    MENU_MODE,
    READ_TAG_MODE,
    WRITE_TAG_MODE,
    READ_UID_MODE,
    PN_INFO_MODE,
    SAVE_MODE,
    LOAD_MODE,
    MYKEY_INFO_MODE
};

class MAVAITool {
public:
    MAVAITool();
    MAVAITool(bool headless_mode);  // Headless constructor for serial use
    ~MAVAITool();

    bool initHardware();
    void setup();
    void loop();

    // Serial operations
    bool read_tag_serial();
    bool write_tag_serial();

    // File operations
    bool save_file_data(FS *fs, String filepath);
    void load_file_data(FS *fs, String filepath);

    // Credit operations
    uint16_t getCurrentCredit();
    uint16_t getPreviousCredit();
    bool setCredit(uint16_t cents);
    bool addCredit(uint16_t cents);

    // Vendor operations
    void exportVendor(uint32_t *vendor);
    void importVendor(uint32_t vendor);

    // Key operations
    bool resetKey();
    bool isReset();
    uint32_t getEncryptionKey() { return _encryptionKey; }
    uint32_t getKeyID();
    String getProductionDate();

    // Dump state
    bool isDumpValidFromRead() { return _dump_valid_from_read; }
    bool isDumpValidFromLoad() { return _dump_valid_from_load; }
    bool isDumpModified() { return _dump_modified; }
    const uint8_t* getDump() { return _dump; }
    const uint8_t* getUID() { return _uid; }

    // Restore from backup
    void restoreDumpFromBackup(const uint8_t* backupDump, const uint8_t* backupUid);

private:
    Arduino_PN532_SRIX *nfc;
    bool _has_hardware_pins;

    MAVAI_State current_state;
    bool _tag_read;
    bool _screen_drawn;
    uint32_t _lastReadTime;

    // RAM storage
    uint8_t _dump[SRIX4K_BYTES];
    uint8_t _uid[SRIX_UID_LENGTH];
    bool _dump_valid_from_read;
    bool _dump_valid_from_load;
    bool _dump_modified;

    // MyKey data
    uint32_t _encryptionKey;
    uint32_t _masterKey;
    uint32_t _currentVendor;
    bool _vendorCalculated;

    // Block flags
    SrixBlockFlags _blockFlags;

    // Block tracking
    void srixFlagClear();
    void srixFlagAdd(uint8_t blockNum);
    bool srixFlagGet(uint8_t blockNum);

    // Block operations
    uint8_t getBlockType(uint8_t blockNum);
    bool isBlockWritable(uint8_t blockNum);
    bool validateManufacturerCode();
    void modifyBlock(uint8_t blockNum, uint8_t *data);
    uint8_t writeModifiedBlocksToTag();

    // MyKey helpers
    uint32_t* getBlockPtr(uint8_t blockNum);
    uint64_t getUidAsUint64();
    uint32_t getUidForEncryption();
    uint32_t readBlockAsUint32(uint8_t blockNum);
    void writeBlockAsUint32(uint8_t blockNum, uint32_t value);
    void writeBlockToMemory(uint8_t blockNum, uint32_t value);
    uint32_t byteSwap32(uint32_t value);
    uint32_t calculateOTP(uint32_t otpBlock);
    void calculateEncryptionKey();
    void encodeDecodeBlock(uint32_t *block);
    String toHex32(uint32_t value);
    String formatCreditEUR(uint16_t cents);
    void progressHandler(uint8_t current, uint8_t total, const char* label);

    // UI methods
    void display_banner();
    void printSubtitle(const char* text);
    void set_state(MAVAI_State state);
    void select_state();
    bool waitForTag();
    void delayWithReturn(uint32_t ms);

    void show_main_menu();
    void read_tag();
    void write_tag();
    void read_uid();
    void show_pn_info();
    void save_file();
    void load_file();
    void show_mykey_info();

    // Credit UI
    void view_credit_ui();
    void add_credit_ui(uint16_t amount = 0);
    void sub_credit_ui(uint16_t amount = 0);
    void set_credit_ui();

    // Vendor UI
    void view_vendor_ui();
    void import_vendor_ui();
    void export_vendor_ui();

    // Reset UI
    void reset_key_ui();
};

void MAVAI_Tool();

#endif

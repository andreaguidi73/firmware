/**
 * @file mavai.cpp
 * @brief MAVAI - Advanced MyKey/SRIX4K NFC Tool v1.0
 * @info Based on SRIX Tool (PR #1983) with complete MyKey functionality
 * @date 2026-01-17
 */
#include "mavai.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/settings.h"
#include <vector>
#include <time.h>

#define TAG_TIMEOUT_MS 500
#define TAG_MAX_ATTEMPTS 5

MAVAITool::MAVAITool() {
    current_state = IDLE_MODE;
    _dump_valid_from_read = false;
    _dump_valid_from_load = false;
    _dump_modified = false;
    _tag_read = false;
    _encryptionKey = 0;
    _currentVendor = 0;
    _vendorCalculated = false;
    setup();
}

MAVAITool::~MAVAITool() { delete nfc; }

void MAVAITool::setup() {
    // Use I2C pins from global config
    int sda_pin = bruceConfigPins.i2c_bus.sda;
    int scl_pin = bruceConfigPins.i2c_bus.scl;

    drawMainBorderWithTitle("MAVAI TOOL");
    padprintln("");
    padprintln("Initializing I2C...");

    // Init I2C
    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(100000);

    padprintln("Initializing PN532...");

// Create PN532 object based on board
#if defined(PN532_IRQ) && defined(PN532_RF_REST)
    // Board with integrated PN532 (e.g. T-Embed)
    nfc = new Arduino_PN532_SRIX(PN532_IRQ, PN532_RF_REST);
    padprintln("Hardware mode (IRQ + RST)");
#else
    // Board with external PN532 I2C (e.g. CYD)
    nfc = new Arduino_PN532_SRIX(-1, -1);
    padprintln("I2C-only mode");
#endif

    if (!nfc->init()) {
        displayError("PN532 not found!", true);
        return;
    }

    padprintln("Init OK, testing retries...");

    // Configure for SRIX
    if (!nfc->setPassiveActivationRetries(0xFF)) {
        displayError("Retry config failed!", true);
        delay(500);
        return;
    }

    padprintln("Testing SRIX init...");
    if (!nfc->SRIX_init()) {
        displayError("SRIX init failed!", true);
        return;
    }
    uint32_t ver = nfc->getFirmwareVersion();
    if (ver) {
        uint8_t chip = (ver >> 24) & 0xFF;
        uint8_t fw_major = (ver >> 16) & 0xFF;
        uint8_t fw_minor = (ver >> 8) & 0xFF;

        padprintln("Chip: PN5" + String(chip, HEX));
        padprintln("FW: " + String(fw_major) + "." + String(fw_minor));
    }
    delay(2000);
    displaySuccess("PN532-SRIX ready!");
    delay(2500);

    set_state(IDLE_MODE);
    return loop();
}

void MAVAITool::loop() {
    while (1) {
        if (check(EscPress)) {
            returnToMenu = true;
            break;
        }

        if (check(SelPress)) { select_state(); }

        switch (current_state) {
            case IDLE_MODE: show_main_menu(); break;
            case READ_TAG_MODE: read_tag(); break;
            case WRITE_TAG_MODE: write_tag(); break;
            case READ_UID_MODE: read_uid(); break;
            case PN_INFO_MODE: show_pn_info(); break;
            case SAVE_MODE: save_file(); break;
            case LOAD_MODE:
                if (_screen_drawn) {
                    delay(50);
                } else {
                    load_file();
                }
                break;
            case MYKEY_INFO_MODE: show_mykey_info(); break;
        }
    }
}

void MAVAITool::select_state() {
    options = {};

    options.emplace_back("Main Menu", [this]() { set_state(IDLE_MODE); });
    options.emplace_back("Read Tag", [this]() { set_state(READ_TAG_MODE); });

    if (_dump_valid_from_read) {
        options.emplace_back(" -Save dump", [this]() { set_state(SAVE_MODE); });
    }
    
    options.emplace_back("Read UID", [this]() { set_state(READ_UID_MODE); });
    options.emplace_back("Load Dump", [this]() { set_state(LOAD_MODE); });
    
    // Show "Write to tag" option if:
    // - Dump was loaded from file (_dump_valid_from_load), OR
    // - Data was read from tag AND modified (_dump_valid_from_read && _dump_modified)
    if (_dump_valid_from_load || (_dump_valid_from_read && _dump_modified)) {
        options.emplace_back(" -Write to tag", [this]() { set_state(WRITE_TAG_MODE); });
    }
    
    // MyKey Tools submenu - only show if we have data in memory
    if (_dump_valid_from_read || _dump_valid_from_load) {
        options.emplace_back("--- MyKey Tools ---", []() {});
        options.emplace_back("View Credit", [this]() { view_credit_ui(); });
        options.emplace_back("Add Credit", [this]() { add_credit_ui(); });
        options.emplace_back("Set Credit", [this]() { set_credit_ui(); });
        options.emplace_back("View Vendor", [this]() { view_vendor_ui(); });
        options.emplace_back("Import Vendor", [this]() { import_vendor_ui(); });
        options.emplace_back("Export Vendor", [this]() { export_vendor_ui(); });
        options.emplace_back("Reset Key", [this]() { reset_key_ui(); });
        options.emplace_back("MyKey Info", [this]() { set_state(MYKEY_INFO_MODE); });
    }
    
    options.emplace_back("Clone Tag", [this]() { set_state(WRITE_TAG_MODE); });
    options.emplace_back("PN532 Info", [this]() { set_state(PN_INFO_MODE); });

    loopOptions(options);
}

void MAVAITool::set_state(MAVAI_State state) {
    current_state = state;
    _tag_read = false;
    _screen_drawn = false;
    if (state == READ_TAG_MODE) { _dump_valid_from_load = false; }
    display_banner();
    delay(300);
}

void MAVAITool::display_banner() {
    drawMainBorderWithTitle("MAVAI TOOL");

    switch (current_state) {
        case READ_TAG_MODE: printSubtitle("READ TAG MODE"); break;
        case WRITE_TAG_MODE: printSubtitle("WRITE TAG MODE"); break;
        case READ_UID_MODE: printSubtitle("READ UID MODE"); break;
        case PN_INFO_MODE: printSubtitle("PN532 INFO"); break;
        case IDLE_MODE: printSubtitle("MAIN MENU"); break;
        case SAVE_MODE: printSubtitle("SAVE MODE"); break;
        case LOAD_MODE: printSubtitle("LOAD MODE"); break;
        case MYKEY_INFO_MODE: printSubtitle("MYKEY INFO"); break;
    }

    tft.setTextSize(FP);
    padprintln("");
}

void MAVAITool::show_main_menu() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);

    padprintln("=== MAVAI Tool v1.0 ===");
    tft.setTextSize(FP);
    padprintln("");
    padprintln("Advanced MyKey/SRIX4K Tool");
    padprintln("");
    padprintln("Features:");
    padprintln("- Read/Write complete tag");
    padprintln("- MyKey credit management");
    padprintln("- Vendor import/export");
    padprintln("- Encryption key calc");
    padprintln("- Save/Load .mavai dumps");
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to open menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _screen_drawn = true;
}

bool MAVAITool::waitForTag() {
    static uint8_t attempts = 0;
    static unsigned long attemptStart = 0;
    static bool message_shown = false;
    static unsigned long lastDotTime = 0;

    if (millis() - _lastReadTime < 2000) return false;

    if (attempts == 0 && !message_shown) {
        attemptStart = millis();
        padprint("Waiting for tag");
        message_shown = true;
        lastDotTime = millis();
    }

    // Show progress dots every second
    if (message_shown && millis() - lastDotTime > TAG_TIMEOUT_MS) {
        tft.print(".");
        lastDotTime = millis();
    }

    unsigned long elapsed = millis() - attemptStart;
    if (elapsed > TAG_TIMEOUT_MS) {
        attempts++;
        attemptStart = millis();

        if (attempts >= TAG_MAX_ATTEMPTS) {
            padprintln("");
            padprintln("");
            padprintln("Timeout! No tag found.");
            attempts = 0;
            message_shown = false;
            delay(1000);
            display_banner();
            return false;
        }
    }

    if (nfc->SRIX_initiate_select()) {
        attempts = 0;
        message_shown = false;
        padprintln("");
        return true;
    }

    return false;
}

void MAVAITool::read_tag() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    if (!waitForTag()) return;

    display_banner();

    _dump_valid_from_read = false;
    padprintln("Tag detected!");
    padprintln("");

    // Read UID
    if (!nfc->SRIX_get_uid(_uid)) {
        displayError("Failed to read UID!");
        delay(2000);
        set_state(READ_TAG_MODE);
        return;
    }

    // Show UID
    String uid_str = "";
    for (uint8_t i = 0; i < 8; i++) {
        if (_uid[i] < 0x10) uid_str += "0";
        uid_str += String(_uid[i], HEX);
        if (i < 7) uid_str += " ";
    }
    uid_str.toUpperCase();
    padprintln("UID: " + uid_str);
    padprintln("");

    // Read 128 blocks
    padprintln("Reading 128 blocks...");
    padprint("Please Wait");
    uint8_t block[4];

    for (uint8_t b = 0; b < SRIX4K_BLOCKS; b++) {
        if (!nfc->SRIX_read_block(b, block)) {
            displayError("Read failed at block " + String(b));
            delay(2000);
            set_state(READ_TAG_MODE);
            return;
        }

        const uint16_t off = (uint16_t)b * 4;
        _dump[off + 0] = block[0];
        _dump[off + 1] = block[1];
        _dump[off + 2] = block[2];
        _dump[off + 3] = block[3];

        // Progress indicator
        progressHandler(b + 1, SRIX4K_BLOCKS, "Reading data blocks");
    }

    _dump_valid_from_read = true;
    _dump_valid_from_load = false;
    _dump_modified = false; // Reset modified flag when reading a new tag
    _vendorCalculated = false; // Reset vendor calculation flag
    
    padprintln("");
    padprintln("");
    displaySuccess("Tag read successfully!");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");

    // Show quick MyKey info
    tft.setTextSize(FP);
    calculateEncryptionKey();
    uint16_t credit = getCurrentCredit();
    float creditEuro = credit / 100.0;
    padprintln("Credit: EUR " + String(creditEuro, 2));
    
    uint32_t vendor = 0;
    exportVendor(&vendor);
    padprintln("Vendor: 0x" + String(vendor, HEX));
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] for Main Menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _tag_read = true;
    _lastReadTime = millis();
    _screen_drawn = true;
}

void MAVAITool::write_tag() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        displayError("Read or load a dump first.");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    if (!waitForTag()) return;

    display_banner();
    padprintln("Tag detected!");
    padprintln("");
    padprintln("Writing 128 blocks...");
    padprintln("");
    padprint("Please Wait");

    uint8_t block[4];
    uint8_t blocks_written = 0;
    uint8_t blocks_failed = 0;
    String failed_blocks = "";

    for (uint8_t b = 0; b < SRIX4K_BLOCKS; b++) {
        const uint16_t off = (uint16_t)b * 4;
        block[0] = _dump[off + 0];
        block[1] = _dump[off + 1];
        block[2] = _dump[off + 2];
        block[3] = _dump[off + 3];

        if (!nfc->SRIX_write_block(b, block)) {
            blocks_failed++;
            if (blocks_failed <= 10) {
                if (failed_blocks.length() > 0) failed_blocks += ",";
                failed_blocks += String(b);
            }
        } else {
            blocks_written++;
        }

        progressHandler(b + 1, SRIX4K_BLOCKS, "Writing data blocks");
    }

    padprintln("");
    padprintln("");

    // Final report
    if (blocks_failed == 0) {
        displaySuccess("Write complete!", true);
        _dump_modified = false; // Reset modified flag after successful write
    } else if (blocks_written > 0) {
        displayWarning("Partial write!", true);
        padprintln("");
        padprintln("Written: " + String(blocks_written) + "/128");
        padprintln("Failed: " + String(blocks_failed));
        padprintln("");
        tft.setTextSize(FP);

        if (blocks_failed > 10) {
            padprintln("Failed blocks: " + failed_blocks + "...");
        } else {
            padprintln("Failed blocks: " + failed_blocks);
        }
    } else {
        displayError("Write failed!", true);
        padprintln("No blocks written");
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    _lastReadTime = millis();
    delayWithReturn(3000);
    set_state(IDLE_MODE);
}

void MAVAITool::read_uid() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    if (!waitForTag()) return;

    display_banner();

    padprintln("Tag detected!");
    padprintln("");
    padprintln("");

    if (!nfc->SRIX_get_uid(_uid)) {
        displayError("Failed to read UID!");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        delay(2000);
        set_state(READ_UID_MODE);
        return;
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    
    String uid_line = "UID: ";
    for (uint8_t i = 0; i < 8; i++) {
        if (_uid[i] < 0x10) uid_line += "0";
        uid_line += String(_uid[i], HEX);
        if (i == 1 || i == 3 || i == 5) { uid_line += " "; }
    }
    uid_line.toUpperCase();
    padprintln(uid_line);
    padprintln("");
    tft.setTextSize(FP);
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] for Main Menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _lastReadTime = millis();
    _screen_drawn = true;
}

void MAVAITool::show_pn_info() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    uint32_t ver = nfc->getFirmwareVersion();
    if (!ver) {
        displayError("Failed to read firmware!");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("PN532 Info:");
    tft.setTextSize(FP);
    padprintln("");

    uint8_t chip = (ver >> 24) & 0xFF;
    uint8_t fw_major = (ver >> 16) & 0xFF;
    uint8_t fw_minor = (ver >> 8) & 0xFF;

    padprintln("Chip: PN5" + String(chip, HEX));
    padprintln("");
    padprintln("Firmware: " + String(fw_major) + "." + String(fw_minor));
    padprintln("");

    if (_has_hardware_pins) {
        padprintln("Mode: Hardware (IRQ+RST)");
    } else {
        padprintln("Mode: I2C-Only (Polling)");
    }

    _screen_drawn = true;
}

void MAVAITool::save_file() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        displayError("Read or load a tag first.");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    // Build UID string with CORRECT order (D002... first)
    String uid_str = "";
    for (int i = 7; i >= 0; i--) {
        if (_uid[i] < 0x10) uid_str += "0";
        uid_str += String(_uid[i], HEX);
    }
    uid_str.toUpperCase();

    // Ask the user for the file name
    String filename = keyboard(uid_str, 30, "File name:");
    filename.trim();

    if (filename == "\x1B") {
        padprintln("Operation cancelled.");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    if (filename.isEmpty()) {
        displayError("Invalid filename!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    display_banner();

    // Get filesystem
    FS *fs;
    if (!getFsStorage(fs)) {
        displayError("Filesystem error!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    // Create directory if it does not exist
    if (!(*fs).exists("/BruceRFID")) (*fs).mkdir("/BruceRFID");
    if (!(*fs).exists("/BruceRFID/MAVAI")) (*fs).mkdir("/BruceRFID/MAVAI");

    // Check if the file already exists and add number
    String filepath = "/BruceRFID/MAVAI/" + filename;
    if ((*fs).exists(filepath + ".mavai")) {
        int i = 1;
        while ((*fs).exists(filepath + "_" + String(i) + ".mavai")) i++;
        filepath += "_" + String(i);
    }
    filepath += ".mavai";

    // Open file for writing
    File file = (*fs).open(filepath, FILE_WRITE);
    if (!file) {
        displayError("Error creating file!");
        delay(1500);
        set_state(IDLE_MODE);
        return;
    }

    // Calculate all values
    calculateEncryptionKey();
    
    // Get raw block values
    uint32_t block5 = readBlockAsUint32(0x05);
    uint32_t block6 = readBlockAsUint32(MYKEY_BLOCK_OTP);
    uint32_t block18_raw = readBlockAsUint32(MYKEY_BLOCK_VENDOR1);
    uint32_t block19_raw = readBlockAsUint32(MYKEY_BLOCK_VENDOR2);
    uint32_t block21_raw = readBlockAsUint32(MYKEY_BLOCK_CREDIT1);
    uint32_t block23_raw = readBlockAsUint32(MYKEY_BLOCK_PREVCREDIT1);
    
    // Decode vendor blocks
    uint32_t b18_dec = block18_raw;
    uint32_t b19_dec = block19_raw;
    encodeDecodeBlock(&b18_dec);
    encodeDecodeBlock(&b19_dec);
    
    // Get calculated values
    uint32_t vendor = 0;
    exportVendor(&vendor);
    uint16_t credit = getCurrentCredit();
    uint16_t prevCredit = getPreviousCredit();
    uint32_t keyID = getKeyID();
    String prodDate = getProductionDate();
    
    // Calculate OTP values
    uint32_t otpSwapped = byteSwap32(block6);
    uint32_t otp = calculateOTP(block6);
    
    // Lock ID and Bound status
    bool lockIdSet = (block5 & 0x000000FF) == 0x7F;
    bool isBound = !isReset();
    
    // ===== COUNTDOWN COUNTER (Days since 1/1/1995 to production date) =====
    // Stored in BCD format in blocks 0x10, 0x14, 0x3F, 0x43
    // Block format: [CHECKSUM][KEY_ID_BYTE][DAYS_BCD_HIGH][DAYS_BCD_LOW]
    
    uint32_t countdownCounter = 0;
    bool countdownValid = false;
    String countdownSource = "";
    
    // Read from the four possible blocks
    uint32_t block10 = readBlockAsUint32(0x10);
    uint32_t block14 = readBlockAsUint32(0x14);
    uint32_t block3F = readBlockAsUint32(0x3F);
    uint32_t block43 = readBlockAsUint32(0x43);
    
    // Find first valid block (not 0xFFFFFFFF)
    uint32_t counterBlock = 0xFFFFFFFF;
    uint8_t counterBlockNum = 0;
    
    if (block10 != 0xFFFFFFFF) {
        counterBlock = block10;
        counterBlockNum = 0x10;
        countdownSource = "0x10";
    } else if (block14 != 0xFFFFFFFF) {
        counterBlock = block14;
        counterBlockNum = 0x14;
        countdownSource = "0x14";
    } else if (block3F != 0xFFFFFFFF) {
        counterBlock = block3F;
        counterBlockNum = 0x3F;
        countdownSource = "0x3F";
    } else if (block43 != 0xFFFFFFFF) {
        counterBlock = block43;
        counterBlockNum = 0x43;
        countdownSource = "0x43";
    }
    
    if (counterBlock != 0xFFFFFFFF) {
        // Extract BCD value from lower 2 bytes (bytes 2 and 3)
        // Block format: [CHECK][ID][DAYS_H][DAYS_L]
        uint8_t daysHigh = (counterBlock >> 8) & 0xFF;  // Byte 2: thousands & hundreds
        uint8_t daysLow = counterBlock & 0xFF;          // Byte 3: tens & units
        
        // Decode BCD to decimal
        // daysHigh = 0x49 means 4 (thousands) and 9 (hundreds) = 4900
        // daysLow = 0x26 means 2 (tens) and 6 (units) = 26
        // Total = 4926 days
        
        uint32_t thousands = (daysHigh >> 4) & 0x0F;
        uint32_t hundreds = daysHigh & 0x0F;
        uint32_t tens = (daysLow >> 4) & 0x0F;
        uint32_t units = daysLow & 0x0F;
        
        // Validate BCD digits (each must be 0-9)
        if (thousands <= 9 && hundreds <= 9 && tens <= 9 && units <= 9) {
            countdownCounter = thousands * 1000 + hundreds * 100 + tens * 10 + units;
            countdownValid = true;
        }
    }
    
    // Debug output
    Serial.printf("DEBUG Counter: block10=0x%08X block14=0x%08X block3F=0x%08X block43=0x%08X\n", 
                  block10, block14, block3F, block43);
    Serial.printf("DEBUG Counter: using block %s, value=%u days\n", 
                  countdownSource.c_str(), countdownCounter);
    
    
    // Write enhanced header v1.2
    file.println("Filetype: Bruce MAVAI Dump");
    file.println("Version: 1.2");
    
    file.println("# === IDENTIFICATION ===");
    file.println("UID: " + uid_str);
    file.println("KeyID: " + String(keyID, HEX));
    file.println("ProductionDate: " + prodDate);
    
    file.println("# === STATUS ===");
    file.println("LockID: " + String(lockIdSet ? "LOCKED" : "OK"));
    file.println("LockID_Raw: " + String(block5 & 0xFF, HEX));
    file.println("Bound: " + String(isBound ? "YES" : "NO"));
    
    // Countdown counter - days since 1/1/1995 to production date
    if (countdownValid) {
        file.println("DaysElapsed: " + String(countdownCounter));
        file.println("DaysElapsed_Source: Block " + countdownSource);
        file.println("DaysElapsed_Block10_Raw: " + String(block10, HEX));
        file.println("DaysElapsed_Block14_Raw: " + String(block14, HEX));
    } else {
        file.println("DaysElapsed: INVALID");
        file.println("DaysElapsed_Block10_Raw: " + String(block10, HEX));
        file.println("DaysElapsed_Block14_Raw: " + String(block14, HEX));
    }
    
    file.println("# === OTP CALCULATION ===");
    file.println("OTP_Block6_Raw: " + String(block6, HEX));
    file.println("OTP_ByteSwapped: " + String(otpSwapped, HEX));
    file.println("OTP_TwosComplement: " + String(otp, HEX));
    
    file.println("# === VENDOR CALCULATION ===");
    file.println("Vendor_Block18_Raw: " + String(block18_raw, HEX));
    file.println("Vendor_Block18_Decoded: " + String(b18_dec, HEX));
    file.println("Vendor_Block19_Raw: " + String(block19_raw, HEX));
    file.println("Vendor_Block19_Decoded: " + String(b19_dec, HEX));
    file.println("Vendor_Combined: " + String(vendor, HEX));
    file.println("Vendor_ForEncryption: " + String(vendor + 1, HEX));
    
    file.println("# === ENCRYPTION KEY ===");
    file.println("EncryptionKey: " + String(_encryptionKey, HEX));
    
    file.println("# === CREDIT ===");
    file.println("Credit_Block21_Raw: " + String(block21_raw, HEX));
    file.println("Credit_Cents: " + String(credit));
    file.println("Credit_EUR: " + String(credit / 100.0, 2));
    file.println("PrevCredit_Block23_Raw: " + String(block23_raw, HEX));
    file.println("PrevCredit_Cents: " + String(prevCredit));
    file.println("PrevCredit_EUR: " + String(prevCredit / 100.0, 2));
    
    file.println("# === DUMP DATA ===");
    file.println("Blocks: 128");
    file.println("# Data:");

    // Write all blocks in [XX] format YYYYYYYY
    for (uint8_t block = 0; block < SRIX4K_BLOCKS; block++) {
        uint16_t offset = block * 4;

        String line = "[";
        if (block < 0x10) line += "0";
        line += String(block, HEX);
        line += "] ";

        for (uint8_t i = 0; i < 4; i++) {
            if (_dump[offset + i] < 0x10) line += "0";
            line += String(_dump[offset + i], HEX);
        }

        line.toUpperCase();
        file.println(line);
    }

    file.close();

    displaySuccess("File saved!");
    padprintln("");
    padprintln("Path: " + filepath);

    delayWithReturn(2500);
    set_state(IDLE_MODE);
}

void MAVAITool::load_file() {
    FS *fs;
    if (!getFsStorage(fs)) {
        displayError("Filesystem error!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    // Verify that the directory exists
    if (!(*fs).exists("/BruceRFID/MAVAI")) {
        displayError("No dumps found!");
        delay(1500);
        displayError("Folder /BruceRFID/MAVAI missing");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    // List all .mavai files in the directory
    File dir = (*fs).open("/BruceRFID/MAVAI");
    if (!dir || !dir.isDirectory()) {
        displayError("Cannot open MAVAI folder!");
        delay(1500);
        set_state(IDLE_MODE);
        return;
    }

    // Build file list
    std::vector<String> fileList;
    File file = dir.openNextFile();
    while (file) {
        String filename = String(file.name());
        if (filename.endsWith(".mavai") && !file.isDirectory()) {
            int lastSlash = filename.lastIndexOf('/');
            if (lastSlash >= 0) { filename = filename.substring(lastSlash + 1); }
            fileList.push_back(filename);
        }
        file = dir.openNextFile();
    }
    dir.close();

    if (fileList.empty()) {
        displayError("No .mavai files found!");
        delay(2500);
        set_state(IDLE_MODE);
        return;
    }

    // Show Menu
    display_banner();
    padprintln("Select file to load:");
    padprintln("");

    options = {};
    for (const String &fname : fileList) {
        options.emplace_back(fname, [this, fname, fs]() { load_file_data(fs, "/BruceRFID/MAVAI/" + fname); });
    }
    options.emplace_back("Cancel", [this]() { set_state(IDLE_MODE); });

    loopOptions(options);
}

void MAVAITool::load_file_data(FS *fs, String filepath) {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    display_banner();
    padprintln("Loading: " + filepath);
    padprintln("");

    File file = (*fs).open(filepath, FILE_READ);
    if (!file) {
        displayError("Cannot open file!");
        delay(1500);
        set_state(IDLE_MODE);
        return;
    }

    // Reset buffer
    memset(_dump, 0, sizeof(_dump));
    memset(_uid, 0, sizeof(_uid));
    _dump_valid_from_read = false;
    _vendorCalculated = false;

    bool header_passed = false;
    int blocks_loaded = 0;
    String uid_from_file = "";

    // Parse file line by line
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.isEmpty()) continue;

        // Extract UID from header
        if (line.startsWith("UID:")) {
            uid_from_file = line.substring(5);
            uid_from_file.trim();
            uid_from_file.replace(" ", "");

            // Convert UID to byte array
            for (uint8_t i = 0; i < 8 && i * 2 < uid_from_file.length(); i++) {
                String byteStr = uid_from_file.substring(i * 2, i * 2 + 2);
                _uid[i] = strtoul(byteStr.c_str(), NULL, 16);
            }
            continue;
        }

        // Skip header until "# Data:"
        if (!header_passed) {
            if (line.startsWith("# Data:")) { header_passed = true; }
            continue;
        }

        // Parse blocks in the format: [XX] YYYYYYYY
        if (line.startsWith("[") && line.indexOf("]") > 0) {
            int bracket_end = line.indexOf("]");
            String block_num_str = line.substring(1, bracket_end);
            String data_str = line.substring(bracket_end + 1);
            data_str.trim();
            data_str.replace(" ", "");

            uint8_t block_num = strtoul(block_num_str.c_str(), NULL, 16);

            if (block_num >= SRIX4K_BLOCKS) continue;

            if (data_str.length() >= 8) {
                uint16_t offset = block_num * 4;
                for (uint8_t i = 0; i < 4; i++) {
                    String byteStr = data_str.substring(i * 2, i * 2 + 2);
                    _dump[offset + i] = strtoul(byteStr.c_str(), NULL, 16);
                }
                blocks_loaded++;
            }
        }
    }
    file.close();

    if (blocks_loaded < SRIX4K_BLOCKS) {
        displayError("Incomplete dump!");
        displayError("Loaded " + String(blocks_loaded) + "/128 blocks");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    _dump_valid_from_load = true;
    _dump_valid_from_read = false;
    _dump_modified = false; // Reset modified flag when loading a dump file

    displaySuccess("Dump loaded successfully!");
    delay(1000);

    // Extract filename
    String filename = filepath;
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
    }

    current_state = LOAD_MODE;
    _screen_drawn = false;

    display_banner();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("File: " + filename);
    padprintln("");
    padprintln("UID: " + uid_from_file);
    tft.setTextSize(FP);
    padprintln("");
    padprintln("Blocks: " + String(blocks_loaded));
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to open menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _lastReadTime = millis();
    _screen_drawn = true;
}

void MAVAITool::delayWithReturn(uint32_t ms) {
    auto tm = millis();
    while (millis() - tm < ms && !returnToMenu) { vTaskDelay(pdMS_TO_TICKS(50)); }
}

// ============================================================================
// MyKey Functionality Implementation
// ============================================================================

// Helper: Get pointer to a block in the dump
uint32_t* MAVAITool::getBlockPtr(uint8_t blockNum) {
    if (blockNum >= SRIX4K_BLOCKS) return nullptr;
    return (uint32_t*)(&_dump[blockNum * SRIX_BLOCK_LENGTH]);
}

// Helper: Get UID as uint64_t (big-endian)
// UID byte order: _uid[7] _uid[6] ... _uid[1] _uid[0]
// We need to reverse the order so manufacturer code (at _uid[7] and _uid[6]) comes first
uint64_t MAVAITool::getUidAsUint64() {
    uint64_t uid = 0;
    // Read in reverse order: start from _uid[7] (MSB) to _uid[0] (LSB)
    for (int i = 7; i >= 0; i--) {
        uid = (uid << 8) | _uid[i];
    }
    return uid;
}

// Helper: Get UID for encryption (first 4 bytes in little-endian)
uint32_t MAVAITool::getUidForEncryption() {
    // MyKey uses only first 4 bytes of UID in little-endian order
    return ((uint32_t)_uid[3] << 24) | ((uint32_t)_uid[2] << 16) | 
           ((uint32_t)_uid[1] << 8) | (uint32_t)_uid[0];
}

// Helper: Read a block as uint32_t (BIG-ENDIAN from SRIX4K)
uint32_t MAVAITool::readBlockAsUint32(uint8_t blockNum) {
    if (blockNum >= SRIX4K_BLOCKS) return 0;
    uint8_t *ptr = &_dump[blockNum * SRIX_BLOCK_LENGTH];
    // Read as BIG-ENDIAN: ptr[0] = MSB
    return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) |
           ((uint32_t)ptr[2] << 8) | (uint32_t)ptr[3];
}

// Helper: Write a uint32_t to a block (BIG-ENDIAN to SRIX4K)
void MAVAITool::writeBlockAsUint32(uint8_t blockNum, uint32_t value) {
    if (blockNum >= SRIX4K_BLOCKS) return;
    uint8_t *ptr = &_dump[blockNum * SRIX_BLOCK_LENGTH];
    // Write as BIG-ENDIAN: ptr[0] = MSB
    ptr[0] = (uint8_t)((value >> 24) & 0xFF);
    ptr[1] = (uint8_t)((value >> 16) & 0xFF);
    ptr[2] = (uint8_t)((value >> 8) & 0xFF);
    ptr[3] = (uint8_t)(value & 0xFF);
}

// Helper: Write uint32_t to memory (alias for consistency with problem statement)
void MAVAITool::writeBlockToMemory(uint8_t blockNum, uint32_t value) {
    writeBlockAsUint32(blockNum, value);
}

// Helper: Byte swap 32-bit value (reverse byte order)
uint32_t MAVAITool::byteSwap32(uint32_t value) {
    return ((value & 0x000000FF) << 24) | 
           ((value & 0x0000FF00) << 8)  |
           ((value & 0x00FF0000) >> 8)  | 
           ((value & 0xFF000000) >> 24);
}

// Helper: Calculate OTP from raw block value (byte swap + two's complement)
uint32_t MAVAITool::calculateOTP(uint32_t otpBlock) {
    // Byte swap: reverse byte order
    uint32_t otpSwapped = byteSwap32(otpBlock);
    // Two's complement (NOT + 1)
    return ~otpSwapped + 1;
}

// Cryptographic function: XOR-based bit swapping for obfuscation
void MAVAITool::encodeDecodeBlock(uint32_t *block) {
    if (!block) return;
    
    uint32_t val = *block;
    
    // First transformation
    val ^= ((val & 0x00C00000) << 6) | ((val & 0x0000C000) << 12) | ((val & 0x000000C0) << 18) |
           ((val & 0x000C0000) >> 6) | ((val & 0x00030000) >> 12) | ((val & 0x00000300) >> 6);
    
    // Second transformation
    val ^= ((val & 0x30000000) >> 6) | ((val & 0x0C000000) >> 12) | ((val & 0x03000000) >> 18) |
           ((val & 0x00003000) << 6) | ((val & 0x00000030) << 12) | ((val & 0x0000000C) << 6);
    
    // Third transformation
    val ^= ((val & 0x00C00000) << 6) | ((val & 0x0000C000) << 12) | ((val & 0x000000C0) << 18) |
           ((val & 0x000C0000) >> 6) | ((val & 0x00030000) >> 12) | ((val & 0x00000300) >> 6);
    
    *block = val;
}

// Calculate block checksum
void MAVAITool::calculateBlockChecksum(uint32_t *block, uint8_t blockNum) {
    if (!block) return;
    
    // Sum only nibbles 0-5 (bits 0-23)
    uint8_t checksum = 0xFF - blockNum 
                       - (*block & 0x0F)
                       - ((*block >> 4) & 0x0F)
                       - ((*block >> 8) & 0x0F)
                       - ((*block >> 12) & 0x0F)
                       - ((*block >> 16) & 0x0F)
                       - ((*block >> 20) & 0x0F);
    
    // Clear high byte, set checksum
    *block = (*block & 0x00FFFFFF) | ((uint32_t)checksum << 24);
}

// Calculate encryption key: SK = (UID × Vendor × OTP)
void MAVAITool::calculateEncryptionKey() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        _encryptionKey = 0;
        _vendorCalculated = false;
        return;
    }
    
    // 1. OTP Calculation - BYTE SWAP + TWO'S COMPLEMENT
    uint32_t block6 = readBlockAsUint32(MYKEY_BLOCK_OTP);  // 0x06
    uint32_t otp = calculateOTP(block6);
    
    // 2. Vendor Extraction - DECODE FIRST, EXTRACT UPPER 16 BITS, +1
    uint32_t block18 = readBlockAsUint32(MYKEY_BLOCK_VENDOR1);  // 0x18
    uint32_t block19 = readBlockAsUint32(MYKEY_BLOCK_VENDOR2);  // 0x19
    
    // Decode blocks temporarily
    encodeDecodeBlock(&block18);
    encodeDecodeBlock(&block19);
    
    // Extract UPPER 16 bits from each, combine, ADD 1 for encryption
    uint32_t vendorBase = (((block18 >> 16) & 0xFFFF) << 16) | ((block19 >> 16) & 0xFFFF);
    uint32_t vendor = vendorBase + 1;  // CRITICAL: +1 after extraction for encryption key only!
    
    // Note: blocks are not re-encoded because we only used temporary copies
    
    // 3. UID - Use FULL 64-bit value
    uint64_t uid = getUidAsUint64();
    
    // 4. Final calculation: SK = UID * vendor * OTP (truncated to 32-bit)
    _encryptionKey = (uint32_t)(uid * vendor * otp);
    
    _vendorCalculated = true;
    
    // Debug output
    uint32_t otpSwapped = byteSwap32(block6);
    Serial.printf("DEBUG: OTP raw=0x%08X swapped=0x%08X final=0x%08X\n", block6, otpSwapped, otp);
    Serial.printf("DEBUG: Vendor=0x%08X (+1=0x%08X)\n", vendorBase, vendor);
    Serial.printf("DEBUG: UID=0x%016llX\n", uid);
    Serial.printf("DEBUG: EncryptionKey=0x%08X\n", _encryptionKey);
}

// Import vendor code to blocks 0x18, 0x19, 0x1C, 0x1D
void MAVAITool::importVendor(uint32_t vendor) {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return;
    
    // 1. Decode credit blocks with OLD encryption key
    if (!_vendorCalculated) calculateEncryptionKey();
    uint32_t credit21 = readBlockAsUint32(0x21);
    uint32_t credit25 = readBlockAsUint32(0x25);
    credit21 ^= _encryptionKey;
    credit25 ^= _encryptionKey;
    
    // Vendor is stored as (vendor - 1)
    uint32_t vendorMinusOne = vendor - 1;
    
    // Split into upper 16 bits and lower 16 bits
    uint16_t vendorHigh = (vendorMinusOne >> 16) & 0xFFFF;
    uint16_t vendorLow = vendorMinusOne & 0xFFFF;
    
    // 2. Prepare block 0x18: vendor high in UPPER 16 bits (bits 31-16)
    uint32_t block18 = ((uint32_t)vendorHigh << 16);
    calculateBlockChecksum(&block18, 0x18);
    encodeDecodeBlock(&block18);
    writeBlockToMemory(0x18, block18);
    
    // 3. Prepare block 0x19: vendor low in UPPER 16 bits (bits 31-16)
    uint32_t block19 = ((uint32_t)vendorLow << 16);
    calculateBlockChecksum(&block19, 0x19);
    encodeDecodeBlock(&block19);
    writeBlockToMemory(0x19, block19);
    
    // 4. Recalculate encryption key with NEW vendor
    _vendorCalculated = false;
    calculateEncryptionKey();
    
    // 5. Re-encode credit blocks with NEW encryption key
    credit21 ^= _encryptionKey;
    credit25 ^= _encryptionKey;
    writeBlockToMemory(0x21, credit21);
    writeBlockToMemory(0x25, credit25);
    
    // 6. Prepare backup blocks with CORRECT block numbers for checksum
    uint32_t block1C = ((uint32_t)vendorHigh << 16);
    calculateBlockChecksum(&block1C, 0x1C);  // Block 0x1C checksum (not 0x18!)
    encodeDecodeBlock(&block1C);
    writeBlockToMemory(0x1C, block1C);
    
    uint32_t block1D = ((uint32_t)vendorLow << 16);
    calculateBlockChecksum(&block1D, 0x1D);  // Block 0x1D checksum (not 0x19!)
    encodeDecodeBlock(&block1D);
    writeBlockToMemory(0x1D, block1D);
    
    _currentVendor = vendor;
    _dump_modified = true; // Mark dump as modified
}

// Export vendor code
void MAVAITool::exportVendor(uint32_t *vendor) {
    if (!vendor) return;
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        *vendor = 0;
        return;
    }
    
    // Read and decode block 0x18
    uint32_t block18 = readBlockAsUint32(MYKEY_BLOCK_VENDOR1);
    encodeDecodeBlock(&block18);
    uint16_t vendorHigh = (block18 >> 16) & 0xFFFF;  // UPPER 16 bits
    
    // Read and decode block 0x19
    uint32_t block19 = readBlockAsUint32(MYKEY_BLOCK_VENDOR2);
    encodeDecodeBlock(&block19);
    uint16_t vendorLow = (block19 >> 16) & 0xFFFF;   // UPPER 16 bits
    
    // Concatenate: vendorHigh (upper 16 bits) + vendorLow (lower 16 bits)
    uint32_t vendorRaw = ((uint32_t)vendorHigh << 16) | vendorLow;
    
    // Add 1 because vendor is stored as (vendor - 1)
    *vendor = vendorRaw + 1;
}

// Check if key is in factory reset state
bool MAVAITool::isReset() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    uint32_t block18 = readBlockAsUint32(MYKEY_BLOCK_VENDOR1);
    uint32_t block19 = readBlockAsUint32(MYKEY_BLOCK_VENDOR2);
    
    return (block18 == MYKEY_BLOCK18_RESET && block19 == MYKEY_BLOCK19_RESET);
}

// Get current credit from block 0x21
uint16_t MAVAITool::getCurrentCredit() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    
    if (!_vendorCalculated) calculateEncryptionKey();
    
    uint32_t creditBlock = readBlockAsUint32(MYKEY_BLOCK_CREDIT1);  // 0x21
    
    // Debug
    Serial.printf("DEBUG Credit: raw=0x%08X\n", creditBlock);
    
    creditBlock ^= _encryptionKey;      // 1. XOR with encryption key
    Serial.printf("DEBUG Credit: after XOR=0x%08X\n", creditBlock);
    
    encodeDecodeBlock(&creditBlock);    // 2. Decode
    Serial.printf("DEBUG Credit: after decode=0x%08X\n", creditBlock);
    
    uint16_t credit = (uint16_t)(creditBlock & 0xFFFF);  // 3. Return LOW 16 bits only
    Serial.printf("DEBUG Credit: final=%d (0x%04X)\n", credit, credit);
    
    return credit;
}

// Get previous credit from block 0x23
// Note: Previous credit uses a different storage format than current credit:
// - Block 0x23 (PREVCREDIT1) stores the value with only encoding, NO XOR encryption
// - This allows the system to track the last credit value before the most recent transaction
// - Current credit (block 0x21) uses XOR with encryption key + encoding
uint16_t MAVAITool::getPreviousCredit() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    
    // No need to calculate encryption key - previous credit doesn't use XOR encryption
    uint32_t prevCreditBlock = readBlockAsUint32(MYKEY_BLOCK_PREVCREDIT1);  // 0x23
    encodeDecodeBlock(&prevCreditBlock);    // Decode only - no XOR needed for previous credit
    return (uint16_t)(prevCreditBlock & 0xFFFF);  // Return LOW 16 bits only
}

// Add cents with transaction splitting
bool MAVAITool::addCents(uint16_t cents, uint8_t day, uint8_t month, uint16_t year) {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    // Preliminary checks
    if (isLocked()) {
        displayError("Key has lock ID protection!");
        return false;
    }
    if (isReset()) {
        displayError("Key is not associated with vendor!");
        return false;
    }
    if (readBlockAsUint32(MYKEY_BLOCK_OTP) == 0) {
        displayError("Key OTP is zero - not initialized!");
        return false;
    }
    
    if (!_vendorCalculated) calculateEncryptionKey();
    
    uint16_t precedentCredit;
    uint16_t actualCredit = getCurrentCredit();
    uint8_t current = getCurrentTransactionOffset();
    
    // Transaction splitting loop - exactly like MIKAI!
    do {
        precedentCredit = actualCredit;
        
        if (cents >= 200) {
            cents -= 200;
            actualCredit += 200;
        } else if (cents >= 100) {
            cents -= 100;
            actualCredit += 100;
        } else if (cents >= 50) {
            cents -= 50;
            actualCredit += 50;
        } else if (cents >= 20) {
            cents -= 20;
            actualCredit += 20;
        } else if (cents >= 10) {
            cents -= 10;
            actualCredit += 10;
        } else if (cents >= 5) {
            cents -= 5;
            actualCredit += 5;
        } else {
            actualCredit += cents;
            cents = 0;
        }
        
        // Update transaction pointer (circular 0-7)
        current = (current == (MYKEY_TRANS_HISTORY_SIZE - 1)) ? 0 : current + 1;
        
        // Safety check: ensure offset is in valid range
        if (current >= MYKEY_TRANS_HISTORY_SIZE) {
            displayError("Transaction offset out of range!");
            return false;
        }
        
        // Save transaction history block (0x34 + offset)
        uint32_t txBlock = ((uint32_t)day << 27) | 
                          ((uint32_t)month << 23) | 
                          ((uint32_t)(year % 100) << 16) | 
                          actualCredit;
        writeBlockToMemory(0x34 + current, txBlock);
        
    } while (cents > 0);
    
    // ========== SAVE FINAL CREDIT TO 0x21 AND 0x25 ==========
    uint32_t creditBlock = actualCredit;
    calculateBlockChecksum(&creditBlock, 0x21);
    encodeDecodeBlock(&creditBlock);
    creditBlock ^= _encryptionKey;
    writeBlockToMemory(0x21, creditBlock);
    
    creditBlock = actualCredit;
    calculateBlockChecksum(&creditBlock, 0x25);
    encodeDecodeBlock(&creditBlock);
    creditBlock ^= _encryptionKey;
    writeBlockToMemory(0x25, creditBlock);
    
    // ========== SAVE PREVIOUS CREDIT TO 0x23 AND 0x27 ==========
    uint32_t prevCreditBlock = precedentCredit;
    calculateBlockChecksum(&prevCreditBlock, 0x23);
    encodeDecodeBlock(&prevCreditBlock);
    writeBlockToMemory(0x23, prevCreditBlock);
    
    prevCreditBlock = precedentCredit;
    calculateBlockChecksum(&prevCreditBlock, 0x27);
    encodeDecodeBlock(&prevCreditBlock);
    writeBlockToMemory(0x27, prevCreditBlock);
    
    // ========== SAVE TRANSACTION POINTER TO 0x3C ==========
    uint32_t ptrBlock = (uint32_t)current << 16;
    calculateBlockChecksum(&ptrBlock, 0x3C);
    encodeDecodeBlock(&ptrBlock);
    // XOR with Key ID lower 3 bytes
    uint32_t keyID = readBlockAsUint32(MYKEY_BLOCK_KEYID);
    ptrBlock ^= (keyID & 0x00FFFFFF);
    writeBlockToMemory(MYKEY_BLOCK_TRANS_PTR, ptrBlock);
    
    _dump_modified = true; // Mark dump as modified
    
    return true;
}

// Set specific credit
bool MAVAITool::setCents(uint16_t cents, uint8_t day, uint8_t month, uint16_t year) {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    // Safety check: ensure constant is valid
    static_assert(MYKEY_TRANS_TOTAL_BLOCKS == 9, "Transaction block count must be 9 (0x34-0x3C)");
    
    // Backup current state in case of failure
    uint32_t backup21 = readBlockAsUint32(0x21);
    uint32_t backupTx[MYKEY_TRANS_TOTAL_BLOCKS];
    for (int i = 0; i < MYKEY_TRANS_TOTAL_BLOCKS; i++) {
        backupTx[i] = readBlockAsUint32(MYKEY_BLOCK_TRANS_START + i);
    }
    
    if (!_vendorCalculated) calculateEncryptionKey();
    
    // Reset credit block 0x21 to 0
    uint32_t zeroCredit = 0;
    calculateBlockChecksum(&zeroCredit, 0x21);
    encodeDecodeBlock(&zeroCredit);
    zeroCredit ^= _encryptionKey;
    writeBlockToMemory(0x21, zeroCredit);
    
    // Reset transaction history (0x34-0x3C) to 0xFFFFFFFF
    for (int i = 0x34; i <= 0x3C; i++) {
        writeBlockToMemory(i, 0xFFFFFFFF);
    }
    
    // Now add the cents using addCents
    if (!addCents(cents, day, month, year)) {
        // Restore backup on failure
        writeBlockToMemory(0x21, backup21);
        for (int i = 0; i < MYKEY_TRANS_TOTAL_BLOCKS; i++) {
            writeBlockToMemory(MYKEY_BLOCK_TRANS_START + i, backupTx[i]);
        }
        return false;
    }
    
    // Note: _dump_modified is already set by addCents()
    
    return true;
}

// Check Lock ID protection (block 0x05)
bool MAVAITool::checkLockID() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    uint32_t block05 = readBlockAsUint32(MYKEY_BLOCK_LOCKID);
    return (block05 & 0x000000FF) == 0x7F;
}

// Check if key has lock ID protection (compatibility alias)
bool MAVAITool::isLocked() {
    return checkLockID();
}

// Get current transaction offset (block 0x3C)
uint8_t MAVAITool::getCurrentTransactionOffset() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    
    uint32_t block3C = readBlockAsUint32(MYKEY_BLOCK_TRANS_PTR);  // 0x3C
    
    // If first transaction (never used), return 7 to start at position 0
    if (block3C == 0xFFFFFFFF) {
        return MYKEY_TRANS_HISTORY_SIZE - 1;
    }
    
    // Decode: XOR with Key ID lower 3 bytes, then encodeDecodeBlock
    uint32_t keyID = readBlockAsUint32(MYKEY_BLOCK_KEYID);  // 0x07
    uint32_t decoded = block3C ^ (keyID & 0x00FFFFFF);
    encodeDecodeBlock(&decoded);
    
    // Extract offset from bits 16-23
    uint8_t offset = (decoded >> 16) & 0xFF;
    
    // Validate range 0-7
    return (offset >= MYKEY_TRANS_HISTORY_SIZE) ? (MYKEY_TRANS_HISTORY_SIZE - 1) : offset;
}

// Get Key ID from block 0x07
uint32_t MAVAITool::getKeyID() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    return readBlockAsUint32(MYKEY_BLOCK_KEYID);
}

// Get production date from block 0x08 (BCD format)
String MAVAITool::getProductionDate() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return "N/A";
    
    uint32_t block08 = readBlockAsUint32(MYKEY_BLOCK_PRODDATE);
    
    // BCD decoding
    uint8_t day = ((block08 >> 28) & 0x0F) * 10 + ((block08 >> 24) & 0x0F);
    uint8_t month = ((block08 >> 20) & 0x0F) * 10 + ((block08 >> 16) & 0x0F);
    uint16_t year = ((block08 & 0x0F) * 1000) + 
                    (((block08 >> 4) & 0x0F) * 100) +
                    (((block08 >> 12) & 0x0F) * 10) + 
                    ((block08 >> 8) & 0x0F);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d", day, month, year);
    return String(buf);
}

// Get days since production
uint16_t MAVAITool::getDaysSinceProduction() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    
    uint32_t block08 = readBlockAsUint32(MYKEY_BLOCK_PRODDATE);
    
    // BCD decoding (same as getProductionDate)
    uint8_t day = ((block08 >> 28) & 0x0F) * 10 + ((block08 >> 24) & 0x0F);
    uint8_t month = ((block08 >> 20) & 0x0F) * 10 + ((block08 >> 16) & 0x0F);
    uint16_t year = ((block08 & 0x0F) * 1000) + 
                    (((block08 >> 4) & 0x0F) * 100) +
                    (((block08 >> 12) & 0x0F) * 10) + 
                    ((block08 >> 8) & 0x0F);
    
    // Get current date
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    uint8_t cur_day = timeinfo->tm_mday;
    uint8_t cur_month = timeinfo->tm_mon + 1;
    uint16_t cur_year = timeinfo->tm_year + 1900;
    
    // Calculate difference (simplified)
    uint16_t prod_days = daysDifference(day, month, year);
    uint16_t cur_days = daysDifference(cur_day, cur_month, cur_year);
    
    return (cur_days > prod_days) ? (cur_days - prod_days) : 0;
}

// Calculate days from 1/1/1995 - simple implementation
uint16_t MAVAITool::daysDifference(uint8_t day, uint8_t month, uint16_t year) {
    const uint16_t baseYear = 1995;
    
    if (year < baseYear) return 0;
    
    const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    uint32_t totalDays = 0;
    
    // Add days for complete years
    for (uint16_t y = baseYear; y < year; y++) {
        bool isLeap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        totalDays += isLeap ? 366 : 365;
    }
    
    // Add days for complete months in current year
    bool isLeap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    for (uint8_t m = 1; m < month; m++) {
        totalDays += daysInMonth[m - 1];
        if (m == 2 && isLeap) totalDays++;
    }
    
    // Add days in current month
    totalDays += day - 1;
    
    return (uint16_t)totalDays;
}

// Calculate days since 1/1/1995 using MIKAI algorithm
uint32_t MAVAITool::daysSince1995(uint8_t day, uint8_t month, uint16_t year) {
    // From MIKAI mykey.c daysDifference()
    if (month < 3) {
        year--;
        month += 12;
    }
    return year * 365 + year / 4 - year / 100 + year / 400 + (month * 153 + 3) / 5 + day - 728692;
}

// Read days elapsed from counter blocks (0x10, 0x14, 0x3F, 0x43)
uint32_t MAVAITool::getDaysElapsed() {
    // Try each block in order
    uint32_t blocks[] = {
        readBlockAsUint32(0x10),
        readBlockAsUint32(0x14),
        readBlockAsUint32(0x3F),
        readBlockAsUint32(0x43)
    };
    
    for (int i = 0; i < 4; i++) {
        if (blocks[i] != 0xFFFFFFFF) {
            // Extract BCD from lower 2 bytes
            uint8_t daysHigh = (blocks[i] >> 8) & 0xFF;
            uint8_t daysLow = blocks[i] & 0xFF;
            
            uint32_t thousands = (daysHigh >> 4) & 0x0F;
            uint32_t hundreds = daysHigh & 0x0F;
            uint32_t tens = (daysLow >> 4) & 0x0F;
            uint32_t units = daysLow & 0x0F;
            
            // Validate BCD
            if (thousands <= 9 && hundreds <= 9 && tens <= 9 && units <= 9) {
                return thousands * 1000 + hundreds * 100 + tens * 10 + units;
            }
        }
    }
    
    return 0; // Invalid or not found
}

// Reset key to factory defaults - Complete MIKAI MyKeyReset() implementation
bool MAVAITool::resetKey() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    // Backup current state in case of failure
    uint8_t backup[SRIX4K_BYTES];
    memcpy(backup, _dump, sizeof(backup));
    
    // Get Key ID from block 0x07
    uint32_t keyID = readBlockAsUint32(MYKEY_BLOCK_KEYID);
    uint8_t keyIDFirstByte = (keyID >> 24) & 0xFF;
    
    // Get production date from block 0x08
    uint32_t productionDate = readBlockAsUint32(MYKEY_BLOCK_PRODDATE);
    
    // Decode BCD production date (MIKAI nibble order)
    uint8_t day = ((productionDate >> 28) & 0x0F) * 10 + ((productionDate >> 24) & 0x0F);
    uint8_t month = ((productionDate >> 20) & 0x0F) * 10 + ((productionDate >> 16) & 0x0F);
    uint16_t year = ((productionDate & 0x0F) * 1000) + 
                   (((productionDate >> 4) & 0x0F) * 100) +
                   (((productionDate >> 12) & 0x0F) * 10) + 
                   ((productionDate >> 8) & 0x0F);
    
    // Calculate days from 1/1/1995 using MIKAI algorithm
    uint32_t elapsedDays = daysSince1995(day, month, year);
    
    // Format elapsed days in BCD for blocks 0x10, 0x14, 0x3F, 0x43
    // BCD format: ((elapsed/1000%10)<<12) + ((elapsed/100%10)<<8) + ((elapsed/10%10)<<4) + (elapsed%10)
    uint32_t elapsedBCD = ((elapsedDays / 1000 % 10) << 12) | 
                          ((elapsedDays / 100 % 10) << 8) |
                          ((elapsedDays / 10 % 10) << 4) | 
                          (elapsedDays % 10);
    
    // Process all blocks from 0x10 to end (following MIKAI MyKeyReset logic)
    for (uint8_t i = 0x10; i < SRIX4K_BLOCKS; i++) {
        uint32_t currentBlock = 0xFFFFFFFF; // Default value
        bool shouldWrite = true;
        
        switch (i) {
            case 0x10:
            case 0x14:
            case 0x3F:
            case 0x43: {
                // Key ID (first byte) + days elapsed from production
                // Structure: CHECK | ID_BYTE | DAYS_BCD_H | DAYS_BCD_L
                currentBlock = (keyIDFirstByte << 16) | elapsedBCD;
                calculateBlockChecksum(&currentBlock, i);
                break;
            }
            
            case 0x11:
            case 0x15:
            case 0x40:
            case 0x44: {
                // Key ID [last three bytes] (lower 24 bits of block 0x07)
                currentBlock = keyID & 0x00FFFFFF;
                calculateBlockChecksum(&currentBlock, i);
                break;
            }
            
            case 0x22:
            case 0x26:
            case 0x51:
            case 0x55: {
                // Production date (last three bytes, reordered)
                currentBlock = ((productionDate & 0x0000FF00) << 8) | 
                               ((productionDate & 0x00FF0000) >> 8) | 
                               ((productionDate & 0xFF000000) >> 24);
                calculateBlockChecksum(&currentBlock, i);
                encodeDecodeBlock(&currentBlock);
                break;
            }
            
            case 0x12:
            case 0x16:
            case 0x41:
            case 0x45: {
                // Operations counter (starts at 1)
                currentBlock = 1;
                calculateBlockChecksum(&currentBlock, i);
                break;
            }
            
            case 0x13:
            case 0x17:
            case 0x42:
            case 0x46: {
                // Generic blocks - fixed value
                currentBlock = 0x00040013;
                calculateBlockChecksum(&currentBlock, i);
                break;
            }
            
            case 0x18:
            case 0x1C:
            case 0x47:
            case 0x4B: {
                // Vendor blocks (high part) - reset to factory default
                currentBlock = MYKEY_VENDOR_DEFAULT_HIGH;  // 0x0000FEDC
                calculateBlockChecksum(&currentBlock, i);
                encodeDecodeBlock(&currentBlock);
                break;
            }
            
            case 0x19:
            case 0x1D:
            case 0x48:
            case 0x4C: {
                // Vendor blocks (low part) - reset to factory default
                currentBlock = MYKEY_VENDOR_DEFAULT_LOW;  // 0x00000123
                calculateBlockChecksum(&currentBlock, i);
                encodeDecodeBlock(&currentBlock);
                break;
            }
            
            case 0x21:
            case 0x25: {
                // Current credit (reset to 0.00€)
                // Must recalculate encryption key with new vendor first
                _vendorCalculated = false;
                calculateEncryptionKey();
                currentBlock = 0;
                calculateBlockChecksum(&currentBlock, i);
                encodeDecodeBlock(&currentBlock);
                currentBlock ^= _encryptionKey;
                break;
            }
            
            case 0x20:
            case 0x24:
            case 0x4F:
            case 0x53: {
                // Generic blocks - fixed value
                currentBlock = 0x00010000;
                calculateBlockChecksum(&currentBlock, i);
                encodeDecodeBlock(&currentBlock);
                break;
            }
            
            case 0x1A:
            case 0x1B:
            case 0x1E:
            case 0x1F:
            case 0x23:
            case 0x27:
            case 0x49:
            case 0x4A:
            case 0x4D:
            case 0x4E:
            case 0x50:
            case 0x52:
            case 0x54:
            case 0x56: {
                // Generic blocks (set to 0 with checksum and encoding)
                currentBlock = 0;
                calculateBlockChecksum(&currentBlock, i);
                encodeDecodeBlock(&currentBlock);
                break;
            }
            
            // Transaction history blocks - set to 0xFFFFFFFF
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3A:
            case 0x3B:
            case 0x3C: {
                currentBlock = 0xFFFFFFFF;
                break;
            }
            
            default:
                // Skip other blocks (keep 0xFFFFFFFF or don't modify)
                shouldWrite = false;
                break;
        }
        
        // Write the block if it differs from current value
        if (shouldWrite) {
            uint32_t existingBlock = readBlockAsUint32(i);
            if (existingBlock != currentBlock) {
                writeBlockToMemory(i, currentBlock);
            }
        }
    }
    
    // Reset internal state
    _currentVendor = 0;
    _vendorCalculated = false;
    _dump_modified = true; // Mark dump as modified
    
    Serial.printf("DEBUG resetKey: Reset complete. %d days since 1/1/1995 (BCD: 0x%04X)\n", 
                  elapsedDays, elapsedBCD);
    
    return true;
}

// ============================================================================
// MyKey UI Functions
// ============================================================================

void MAVAITool::show_mykey_info() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }
    
    display_banner();
    
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("=== MAVAI Tool v1.0 ===");
    tft.setTextSize(FP);
    
    // Show UID
    String uid_str = "UID: ";
    for (uint8_t i = 0; i < 8; i++) {
        if (_uid[i] < 0x10) uid_str += "0";
        uid_str += String(_uid[i], HEX);
        if (i < 7) uid_str += " ";
    }
    uid_str.toUpperCase();
    padprintln(uid_str);
    
    // Show Key ID
    uint32_t keyID = getKeyID();
    padprintln("Key ID: " + String(keyID, HEX));
    padprintln("-----------------------");
    
    // Calculate encryption key if not already done
    if (!_vendorCalculated) {
        calculateEncryptionKey();
    }
    
    // Display credit
    uint16_t credit = getCurrentCredit();
    float creditEuro = credit / 100.0;
    padprintln("Credit: EUR " + String(creditEuro, 2));
    
    // Display vendor
    uint32_t vendor = 0;
    exportVendor(&vendor);
    padprintln("Vendor: " + String(vendor, HEX));
    
    // Display encryption key
    padprintln("Enc Key: " + String(_encryptionKey, HEX));
    padprintln("-----------------------");
    
    // Production date
    String prodDate = getProductionDate();
    padprintln("Production: " + prodDate);
    
    // Lock status
    bool locked = checkLockID();
    padprintln("Lock ID: " + String(locked ? "[LOCKED]" : "[OK]"));
    
    // Reset status
    bool resetState = isReset();
    padprintln("Status: " + String(resetState ? "[RESET]" : "[ACTIVE]"));
    padprintln("");
    
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to continue");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    
    _screen_drawn = true;
}

void MAVAITool::view_credit_ui() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        return;
    }
    
    display_banner();
    
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("Current Credit:");
    tft.setTextSize(FP);
    padprintln("");
    
    uint16_t credit = getCurrentCredit();
    float creditEuro = credit / 100.0;
    
    padprintln("EUR " + String(creditEuro, 2));
    padprintln("(" + String(credit) + " cents)");
    padprintln("");
    
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to continue");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    
    delayWithReturn(3000);
}

void MAVAITool::add_credit_ui() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        return;
    }
    
    display_banner();
    
    // Get amount in cents
    String amountStr = num_keyboard("", 10, "Enter cents to add:");
    if (amountStr == "\x1B" || amountStr.isEmpty()) {
        return;
    }
    
    uint16_t cents = amountStr.toInt();
    
    // Get current date
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    
    uint8_t day = timeinfo->tm_mday;
    uint8_t month = timeinfo->tm_mon + 1;
    uint16_t year = timeinfo->tm_year + 1900;
    
    display_banner();
    
    // Add credit
    if (addCents(cents, day, month, year)) {
        displaySuccess("Credit added!");
        padprintln("");
        float creditEuro = cents / 100.0;
        padprintln("Added: EUR " + String(creditEuro, 2));
        padprintln("");
        uint16_t newCredit = getCurrentCredit();
        float newCreditEuro = newCredit / 100.0;
        padprintln("New credit: EUR " + String(newCreditEuro, 2));
        padprintln("");
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        padprintln("Use 'Write to tag' to apply!");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    } else {
        displayError("Failed to add credit!");
    }
    
    delayWithReturn(3000);
}

void MAVAITool::set_credit_ui() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        return;
    }
    
    display_banner();
    
    // Get amount in cents
    String amountStr = num_keyboard("", 10, "Enter new credit (cents):");
    if (amountStr == "\x1B" || amountStr.isEmpty()) {
        return;
    }
    
    uint16_t cents = amountStr.toInt();
    
    // Confirmation dialog
    options = {};
    bool confirmed = false;
    
    options.emplace_back("Confirm", [&confirmed]() { confirmed = true; });
    options.emplace_back("Cancel", []() {});
    
    display_banner();
    padprintln("Set credit to " + String(cents) + " cents?");
    padprintln("");
    padprintln("This will replace the");
    padprintln("current credit value.");
    padprintln("");
    
    loopOptions(options);
    
    if (!confirmed) {
        return;
    }
    
    // Get current date
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    
    uint8_t day = timeinfo->tm_mday;
    uint8_t month = timeinfo->tm_mon + 1;
    uint16_t year = timeinfo->tm_year + 1900;
    
    display_banner();
    
    // Set credit
    if (setCents(cents, day, month, year)) {
        displaySuccess("Credit set!");
        padprintln("");
        float creditEuro = cents / 100.0;
        padprintln("New credit: EUR " + String(creditEuro, 2));
        padprintln("");
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        padprintln("Use 'Write to tag' to apply!");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    } else {
        displayError("Failed to set credit!");
    }
    
    delayWithReturn(3000);
}

void MAVAITool::view_vendor_ui() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        return;
    }
    
    display_banner();
    
    uint32_t vendor = 0;
    exportVendor(&vendor);
    
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("Current Vendor:");
    tft.setTextSize(FP);
    padprintln("");
    padprintln("0x" + String(vendor, HEX));
    padprintln("");
    padprintln("Decimal: " + String(vendor));
    padprintln("");
    
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to continue");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    
    delayWithReturn(3000);
}

void MAVAITool::import_vendor_ui() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        return;
    }
    
    display_banner();
    
    // Get vendor code in hex
    String vendorStr = hex_keyboard("", 8, "Enter vendor code (hex):");
    if (vendorStr == "\x1B" || vendorStr.isEmpty()) {
        return;
    }
    
    // Parse hex string
    uint32_t vendor = strtoul(vendorStr.c_str(), NULL, 16);
    
    // Import vendor
    importVendor(vendor);
    
    displaySuccess("Vendor imported!");
    padprintln("");
    padprintln("Vendor: 0x" + String(vendor, HEX));
    padprintln("");
    tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
    padprintln("Use 'Write to tag' to apply!");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    
    delayWithReturn(3000);
}

void MAVAITool::export_vendor_ui() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        return;
    }
    
    display_banner();
    
    uint32_t vendor = 0;
    exportVendor(&vendor);
    
    displaySuccess("Vendor exported!");
    padprintln("");
    padprintln("Vendor: 0x" + String(vendor, HEX));
    padprintln("");
    padprintln("Use 'Import Vendor' to");
    padprintln("apply to another tag.");
    
    delayWithReturn(3000);
}

void MAVAITool::reset_key_ui() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }
    
    display_banner();
    
    tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
    padprintln("WARNING: Factory Reset");
    padprintln("======================");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");
    padprintln("This will reset ALL MyKey data:");
    padprintln("");
    padprintln("  * Vendor code -> Factory default");
    padprintln("  * Credit -> 0.00 EUR");
    padprintln("  * Transaction history -> Cleared");
    padprintln("  * 38 system blocks -> Recalculated");
    padprintln("  * Counters -> Reset to initial");
    padprintln("");
    tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
    padprintln("PRESERVED:");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("  * UID (hardware, unchangeable)");
    padprintln("  * Key ID");
    padprintln("  * Production Date");
    padprintln("");
    
    options = {};
    bool confirmed = false;
    
    options.emplace_back("CONFIRM Factory Reset", [&confirmed]() { confirmed = true; });
    options.emplace_back("Cancel", [this]() { set_state(IDLE_MODE); });
    
    loopOptions(options);
    
    if (!confirmed) {
        set_state(IDLE_MODE);
        return;
    }
    
    // Second confirmation
    display_banner();
    tft.setTextColor(TFT_RED, bruceConfig.bgColor);
    padprintln("FINAL CONFIRMATION");
    padprintln("==================");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");
    padprintln("Are you ABSOLUTELY SURE?");
    padprintln("");
    padprintln("This action CANNOT be undone");
    padprintln("unless you have a backup!");
    padprintln("");
    
    options = {};
    bool finalConfirm = false;
    
    options.emplace_back("YES - Reset to Factory", [&finalConfirm]() { finalConfirm = true; });
    options.emplace_back("NO - Cancel", [this]() { set_state(IDLE_MODE); });
    
    loopOptions(options);
    
    if (!finalConfirm) {
        set_state(IDLE_MODE);
        return;
    }
    
    // Perform reset
    display_banner();
    padprintln("Performing Factory Reset...");
    padprintln("");
    padprintln("Please wait...");
    
    if (resetKey()) {
        padprintln("");
        displaySuccess("Factory Reset Complete!");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        padprintln("");
        padprintln("• Vendor: Factory default");
        padprintln("• Credit: 0.00€");
        padprintln("• Transaction history: Cleared");
        padprintln("• 38 blocks recalculated");
        padprintln("");
        padprintln("Key is now in factory state.");
        padprintln("");
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        padprintln("Use 'Write to tag' to apply!");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    } else {
        displayError("Reset FAILED!");
        padprintln("");
        padprintln("Data has been restored.");
        padprintln("No changes were made.");
    }
    
    delayWithReturn(5000);
    set_state(IDLE_MODE);
}

// Entry point
void MAVAI_Tool() { MAVAITool mavai_tool; }

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
    
    if (_dump_valid_from_load) {
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

    // Set UID as default filename
    String uid_str = "";
    for (uint8_t i = 0; i < 8; i++) {
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

    // Calculate MyKey data for header
    calculateEncryptionKey();
    uint32_t vendor = 0;
    exportVendor(&vendor);
    uint16_t credit = getCurrentCredit();
    uint32_t keyID = getKeyID();
    String prodDate = getProductionDate();

    // Write header
    file.println("Filetype: Bruce MAVAI Dump");
    file.println("Version: 1.0");
    file.println("UID: " + uid_str);
    file.println("KeyID: " + String(keyID, HEX));
    file.println("Vendor: " + String(vendor, HEX));
    file.println("EncryptionKey: " + String(_encryptionKey, HEX));
    file.println("Credit: " + String(credit));
    file.println("ProductionDate: " + prodDate);
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
uint64_t MAVAITool::getUidAsUint64() {
    uint64_t uid = 0;
    for (int i = 0; i < 8; i++) {
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

// Helper: Read a block as uint32_t (little-endian from SRIX4K)
uint32_t MAVAITool::readBlockAsUint32(uint8_t blockNum) {
    if (blockNum >= SRIX4K_BLOCKS) return 0;
    uint8_t *ptr = &_dump[blockNum * SRIX_BLOCK_LENGTH];
    // Read as little-endian (SRIX4K native format)
    return ((uint32_t)ptr[3] << 24) | ((uint32_t)ptr[2] << 16) |
           ((uint32_t)ptr[1] << 8) | (uint32_t)ptr[0];
}

// Helper: Write a uint32_t to a block (little-endian to SRIX4K)
void MAVAITool::writeBlockAsUint32(uint8_t blockNum, uint32_t value) {
    if (blockNum >= SRIX4K_BLOCKS) return;
    uint8_t *ptr = &_dump[blockNum * SRIX_BLOCK_LENGTH];
    // Write as little-endian (SRIX4K native format)
    ptr[0] = (uint8_t)(value & 0xFF);
    ptr[1] = (uint8_t)((value >> 8) & 0xFF);
    ptr[2] = (uint8_t)((value >> 16) & 0xFF);
    ptr[3] = (uint8_t)((value >> 24) & 0xFF);
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
    
    uint8_t sum = 0;
    uint32_t data = *block & 0xFFFFFF00;
    
    // Sum all nibbles from the upper 3 bytes
    for (int i = 1; i < 8; i++) {
        sum += (data >> (i * 4)) & 0x0F;
    }
    
    uint8_t checksum = 0xFF - blockNum - sum;
    *block = data | checksum;
}

// Calculate encryption key: SK = (UID × Vendor × OTP)
void MAVAITool::calculateEncryptionKey() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        _encryptionKey = 0;
        _vendorCalculated = false;
        return;
    }
    
    // Get OTP from block 0x06
    uint32_t otp = readBlockAsUint32(MYKEY_BLOCK_OTP);
    if (otp == 0) {
        _encryptionKey = 0;
        _vendorCalculated = false;
        return;
    }
    
    // OTP calculation with byte swap and two's complement
    otp = ~((otp << 24) | ((otp & 0x0000FF00) << 8) |
            ((otp & 0x00FF0000) >> 8) | (otp >> 24)) + 1;
    
    // Read and decode vendor blocks to extract vendor
    uint32_t block18 = readBlockAsUint32(MYKEY_BLOCK_VENDOR1);
    encodeDecodeBlock(&block18);
    uint16_t vendorHigh = (block18 >> 16) & 0xFFFF;
    
    uint32_t block19 = readBlockAsUint32(MYKEY_BLOCK_VENDOR2);
    encodeDecodeBlock(&block19);
    uint16_t vendorLow = (block19 >> 16) & 0xFFFF;
    
    // Reconstruct vendor (stored as vendor-1)
    uint32_t vendorRaw = ((uint32_t)vendorHigh << 16) | vendorLow;
    uint32_t vendor = vendorRaw + 1;
    _currentVendor = vendor;
    
    // Calculate encryption key using 4-byte UID (truncated to 32-bit)
    uint32_t uid = getUidForEncryption();
    uint64_t key64 = (uint64_t)uid * (uint64_t)vendor * (uint64_t)otp;
    _encryptionKey = (uint32_t)(key64 & 0xFFFFFFFF);
    _vendorCalculated = true;
}

// Import vendor code to blocks 0x18, 0x19, 0x1C, 0x1D
void MAVAITool::importVendor(uint32_t vendor) {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return;
    
    // Vendor is stored as (vendor - 1)
    uint32_t vendorMinusOne = vendor - 1;
    
    // Split into upper 16 bits and lower 16 bits
    uint16_t vendorHigh = (vendorMinusOne >> 16) & 0xFFFF;
    uint16_t vendorLow = vendorMinusOne & 0xFFFF;
    
    // Prepare block 0x18: vendor high in upper 16 bits
    uint32_t block18 = ((uint32_t)vendorHigh << 16);
    calculateBlockChecksum(&block18, 0x18);
    
    // Prepare block 0x19: vendor low in upper 16 bits
    uint32_t block19 = ((uint32_t)vendorLow << 16);
    calculateBlockChecksum(&block19, 0x19);
    
    // Encode for storage
    encodeDecodeBlock(&block18);
    encodeDecodeBlock(&block19);
    
    // Write primary vendor (blocks 0x18, 0x19)
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR1, block18);
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR2, block19);
    
    // Prepare backup blocks with CORRECT block numbers for checksum
    uint32_t block1C = ((uint32_t)vendorHigh << 16);
    calculateBlockChecksum(&block1C, 0x1C);  // Block 0x1C checksum
    encodeDecodeBlock(&block1C);
    
    uint32_t block1D = ((uint32_t)vendorLow << 16);
    calculateBlockChecksum(&block1D, 0x1D);  // Block 0x1D checksum
    encodeDecodeBlock(&block1D);
    
    // Write backup vendor (blocks 0x1C, 0x1D)
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR1_BACKUP, block1C);
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR2_BACKUP, block1D);
    
    _currentVendor = vendor;
    _vendorCalculated = false;
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
    uint16_t vendorHigh = (block18 >> 16) & 0xFFFF;
    
    // Read and decode block 0x19
    uint32_t block19 = readBlockAsUint32(MYKEY_BLOCK_VENDOR2);
    encodeDecodeBlock(&block19);
    uint16_t vendorLow = (block19 >> 16) & 0xFFFF;
    
    // Concatenate: vendorHigh (upper 16 bits) + vendorLow (lower 16 bits)
    // Example: ABCD + 1234 = ABCD1234
    uint32_t vendorRaw = ((uint32_t)vendorHigh << 16) | vendorLow;
    
    // Stored as vendor-1, so add 1 to get actual vendor
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
    
    uint32_t block21 = readBlockAsUint32(MYKEY_BLOCK_CREDIT1);
    if (block21 == 0) return 0;
    
    // Decode block
    encodeDecodeBlock(&block21);
    
    // Extract credit from upper 16 bits (in cents)
    uint16_t credit = (block21 >> 16) & 0xFFFF;
    
    return credit; // Return in cents, UI divides by 100 for EUR display
}

// Get previous credit from block 0x25
uint16_t MAVAITool::getPreviousCredit() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    
    uint32_t block25 = readBlockAsUint32(MYKEY_BLOCK_CREDIT2);
    if (block25 == 0) return 0;
    
    // Decode block
    encodeDecodeBlock(&block25);
    
    // Extract credit from upper 16 bits
    uint16_t credit = (block25 >> 16) & 0xFFFF;
    
    return credit;
}

// Add cents with transaction splitting
bool MAVAITool::addCents(uint16_t cents, uint8_t day, uint8_t month, uint16_t year) {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    // Get current credit
    uint16_t currentCredit = getCurrentCredit();
    
    // Calculate new credit
    uint16_t newCredit = currentCredit + cents;
    
    // Prepare new block data
    uint32_t newBlock = ((uint32_t)newCredit << 16) | (daysDifference(day, month, year) & 0xFFFF);
    
    // Encode and write to block 0x21
    encodeDecodeBlock(&newBlock);
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT1, newBlock);
    
    // Update transaction history
    uint8_t txOffset = getCurrentTransactionOffset();
    if (txOffset < 8) {
        uint32_t txData = ((uint32_t)cents << 16) | (daysDifference(day, month, year) & 0xFFFF);
        encodeDecodeBlock(&txData);
        writeBlockAsUint32(MYKEY_BLOCK_TRANS_START + txOffset, txData);
        
        // Increment transaction pointer
        writeBlockAsUint32(MYKEY_BLOCK_TRANS_PTR, (txOffset + 1) % 8);
    }
    
    return true;
}

// Set specific credit
bool MAVAITool::setCents(uint16_t cents, uint8_t day, uint8_t month, uint16_t year) {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    // Prepare new block data
    uint32_t newBlock = ((uint32_t)cents << 16) | (daysDifference(day, month, year) & 0xFFFF);
    
    // Encode and write to block 0x21
    encodeDecodeBlock(&newBlock);
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT1, newBlock);
    
    return true;
}

// Check Lock ID protection (block 0x05)
bool MAVAITool::checkLockID() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    uint32_t block05 = readBlockAsUint32(MYKEY_BLOCK_LOCKID);
    
    // Check if first byte is 0x7F (locked)
    uint8_t lockByte = (block05 >> 24) & 0xFF;
    return (lockByte == 0x7F);
}

// Get current transaction offset (block 0x3C)
uint8_t MAVAITool::getCurrentTransactionOffset() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    
    uint32_t block3C = readBlockAsUint32(MYKEY_BLOCK_TRANS_PTR);
    
    return block3C & 0x07;
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
    
    // Extract BCD values (assuming format: DDMMYYYY in BCD)
    uint8_t day = ((block08 >> 28) & 0x0F) * 10 + ((block08 >> 24) & 0x0F);
    uint8_t month = ((block08 >> 20) & 0x0F) * 10 + ((block08 >> 16) & 0x0F);
    uint16_t year = ((block08 >> 12) & 0x0F) * 1000 + ((block08 >> 8) & 0x0F) * 100 + 
                    ((block08 >> 4) & 0x0F) * 10 + (block08 & 0x0F);
    
    // Format as DD/MM/YYYY
    String result = "";
    if (day < 10) result += "0";
    result += String(day) + "/";
    if (month < 10) result += "0";
    result += String(month) + "/" + String(year);
    
    return result;
}

// Get days since production
uint16_t MAVAITool::getDaysSinceProduction() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;
    
    uint32_t block08 = readBlockAsUint32(MYKEY_BLOCK_PRODDATE);
    
    // Extract BCD values
    uint8_t day = ((block08 >> 28) & 0x0F) * 10 + ((block08 >> 24) & 0x0F);
    uint8_t month = ((block08 >> 20) & 0x0F) * 10 + ((block08 >> 16) & 0x0F);
    uint16_t year = ((block08 >> 12) & 0x0F) * 1000 + ((block08 >> 8) & 0x0F) * 100 + 
                    ((block08 >> 4) & 0x0F) * 10 + (block08 & 0x0F);
    
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

// Calculate days from 1/1/1995
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

// Reset key to factory defaults
bool MAVAITool::resetKey() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    
    // Reset primary vendor
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR1, MYKEY_BLOCK18_RESET);
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR2, MYKEY_BLOCK19_RESET);
    
    // Reset backup vendor
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR1_BACKUP, MYKEY_BLOCK18_RESET);
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR2_BACKUP, MYKEY_BLOCK19_RESET);
    
    // Reset credit blocks
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT1, 0);
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT2, 0);
    
    // Reset transaction history
    for (uint8_t i = MYKEY_BLOCK_TRANS_START; i <= MYKEY_BLOCK_TRANS_END; i++) {
        writeBlockAsUint32(i, 0);
    }
    
    // Reset transaction pointer
    writeBlockAsUint32(MYKEY_BLOCK_TRANS_PTR, 0);
    
    _currentVendor = 0;
    _vendorCalculated = false;
    
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
        return;
    }
    
    display_banner();
    
    padprintln("WARNING: This will reset");
    padprintln("the key to factory defaults!");
    padprintln("");
    padprintln("All data will be lost:");
    padprintln("- Vendor will be reset");
    padprintln("- Credit will be 0");
    padprintln("- Transaction history cleared");
    padprintln("");
    
    options = {};
    bool confirmed = false;
    
    options.emplace_back("Confirm Reset", [&confirmed]() { confirmed = true; });
    options.emplace_back("Cancel", []() {});
    
    loopOptions(options);
    
    if (confirmed) {
        display_banner();
        
        if (resetKey()) {
            displaySuccess("Key reset to factory!");
            padprintln("");
            padprintln("Vendor reset to default");
            padprintln("Credit reset to 0");
            padprintln("Transaction history cleared");
        } else {
            displayError("Failed to reset key!");
        }
        
        delayWithReturn(3000);
    }
}

// Entry point
void MAVAI_Tool() { MAVAITool mavai_tool; }

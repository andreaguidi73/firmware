/**
 * @file mavai.cpp
 * @brief MAVAI - Advanced MyKey/SRIX4K NFC Tool v1.0
 * @author andreaguidi73
 * @date 2026-01-17
 */

#include "mavai.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include <globals.h>
#include <vector>

#define MAVAI_TAG_TIMEOUT_MS  100
#define MAVAI_TAG_MAX_ATTEMPTS 5
#define MAVAI_EEPROM_WRITE_DELAY_MS 15

// ==================== CONSTRUCTOR / DESTRUCTOR ====================

MAVAITool::MAVAITool() {
    nfc = nullptr;
    _has_hardware_pins = false;
    current_state = IDLE_MODE;
    _tag_read = false;
    _screen_drawn = false;
    _lastReadTime = 0;
    _dump_valid_from_read = false;
    _dump_valid_from_load = false;
    _dump_modified = false;
    _encryptionKey = 0;
    _masterKey = 0;
    _currentVendor = 0;
    _vendorCalculated = false;
    memset(_dump, 0, sizeof(_dump));
    memset(_uid, 0, sizeof(_uid));
    srixFlagClear();
    setup();
}

MAVAITool::MAVAITool(bool headless_mode) {
    nfc = nullptr;
    _has_hardware_pins = false;
    current_state = IDLE_MODE;
    _tag_read = false;
    _screen_drawn = false;
    _lastReadTime = 0;
    _dump_valid_from_read = false;
    _dump_valid_from_load = false;
    _dump_modified = false;
    _encryptionKey = 0;
    _masterKey = 0;
    _currentVendor = 0;
    _vendorCalculated = false;
    memset(_dump, 0, sizeof(_dump));
    memset(_uid, 0, sizeof(_uid));
    srixFlagClear();
    // Headless mode: no UI, no loop - caller must invoke initHardware() separately
}

MAVAITool::~MAVAITool() {
    delete nfc;
}

// ==================== HARDWARE INIT ====================

bool MAVAITool::initHardware() {
    int sda_pin = bruceConfigPins.i2c_bus.sda;
    int scl_pin = bruceConfigPins.i2c_bus.scl;

    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(100000);

#if defined(PN532_IRQ) && defined(PN532_RF_REST)
    nfc = new Arduino_PN532_SRIX(PN532_IRQ, PN532_RF_REST);
    _has_hardware_pins = true;
#else
    nfc = new Arduino_PN532_SRIX(255, 255);
    _has_hardware_pins = false;
#endif

    if (!nfc->init()) return false;
    if (!nfc->setPassiveActivationRetries(0xFF)) return false;
    if (!nfc->SRIX_init()) return false;
    return true;
}

// ==================== SETUP ====================

void MAVAITool::setup() {
    drawMainBorderWithTitle("MAVAI TOOL");
    padprintln("");
    padprintln("Initializing I2C...");

    int sda_pin = bruceConfigPins.i2c_bus.sda;
    int scl_pin = bruceConfigPins.i2c_bus.scl;

    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(100000);

    padprintln("Initializing PN532...");

#if defined(PN532_IRQ) && defined(PN532_RF_REST)
    nfc = new Arduino_PN532_SRIX(PN532_IRQ, PN532_RF_REST);
    _has_hardware_pins = true;
    padprintln("Hardware mode (IRQ + RST)");
#else
    nfc = new Arduino_PN532_SRIX(255, 255);
    _has_hardware_pins = false;
    padprintln("I2C-only mode");
#endif

    if (!nfc->init()) {
        displayError("PN532 not found!", true);
        return;
    }

    padprintln("Init OK...");

    if (!nfc->setPassiveActivationRetries(0xFF)) {
        displayError("Retry config failed!", true);
        delay(500);
        return;
    }

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

    displaySuccess("MAVAI Tool ready!");
    delay(1000);

    set_state(IDLE_MODE);
    return loop();
}

// ==================== MAIN LOOP ====================

void MAVAITool::loop() {
    while (1) {
        if (check(EscPress)) {
            returnToMenu = true;
            break;
        }

        if (check(SelPress)) { select_state(); }

        switch (current_state) {
            case IDLE_MODE:      show_main_menu(); break;
            case READ_TAG_MODE:  read_tag(); break;
            case WRITE_TAG_MODE: write_tag(); break;
            case READ_UID_MODE:  read_uid(); break;
            case PN_INFO_MODE:   show_pn_info(); break;
            case SAVE_MODE:      save_file(); break;
            case LOAD_MODE:
                if (_screen_drawn) { delay(50); }
                else { load_file(); }
                break;
            case MYKEY_INFO_MODE: show_mykey_info(); break;
            default: delay(50); break;
        }
    }
}

// ==================== STATE MACHINE ====================

void MAVAITool::set_state(MAVAI_State state) {
    current_state = state;
    _tag_read = false;
    _screen_drawn = false;
    display_banner();
    delay(300);
}

void MAVAITool::select_state() {
    options = {};
    options.emplace_back("Main Menu",  [this]() { set_state(IDLE_MODE); });
    options.emplace_back("Read tag",   [this]() { set_state(READ_TAG_MODE); });

    if (_dump_valid_from_read || _dump_valid_from_load) {
        if (_dump_valid_from_read) {
            options.emplace_back(" -Save dump",   [this]() { set_state(SAVE_MODE); });
            options.emplace_back(" -Clone tag",   [this]() { set_state(WRITE_TAG_MODE); });
        }
        options.emplace_back("MyKey Info",     [this]() { set_state(MYKEY_INFO_MODE); });
        options.emplace_back(" -Credit Info",  [this]() {
            set_state(MYKEY_INFO_MODE);
            view_credit_ui();
        });
        options.emplace_back(" -Add Credit",   [this]() { add_credit_ui(); });
        options.emplace_back(" -Set Credit",   [this]() { set_credit_ui(); });
        options.emplace_back(" -Vendor Info",  [this]() { view_vendor_ui(); });
    }
    options.emplace_back("Read UID",   [this]() { set_state(READ_UID_MODE); });
    options.emplace_back("Load dump",  [this]() { set_state(LOAD_MODE); });
    if (_dump_valid_from_load) {
        options.emplace_back(" -Write to tag", [this]() { set_state(WRITE_TAG_MODE); });
    }
    options.emplace_back("PN532 Info", [this]() { set_state(PN_INFO_MODE); });

    loopOptions(options);
}

// ==================== UI HELPERS ====================

void MAVAITool::display_banner() {
    drawMainBorderWithTitle("MAVAI TOOL");

    switch (current_state) {
        case READ_TAG_MODE:   printSubtitle("READ TAG"); break;
        case WRITE_TAG_MODE:  printSubtitle("WRITE TAG"); break;
        case READ_UID_MODE:   printSubtitle("READ UID"); break;
        case PN_INFO_MODE:    printSubtitle("PN532 INFO"); break;
        case IDLE_MODE:       printSubtitle("MAIN MENU"); break;
        case SAVE_MODE:       printSubtitle("SAVE DUMP"); break;
        case LOAD_MODE:       printSubtitle("LOAD DUMP"); break;
        case MYKEY_INFO_MODE: printSubtitle("MYKEY INFO"); break;
        default: break;
    }

    tft.setTextSize(FP);
    padprintln("");
}

void MAVAITool::printSubtitle(const char* text) {
    ::printSubtitle(String(text));
}

void MAVAITool::delayWithReturn(uint32_t ms) {
    auto tm = millis();
    while (millis() - tm < ms && !returnToMenu) { vTaskDelay(pdMS_TO_TICKS(50)); }
}

void MAVAITool::progressHandler(uint8_t current, uint8_t total, const char* label) {
    ::progressHandler((int)current, (size_t)total, String(label));
}

// ==================== MAIN MENU ====================

void MAVAITool::show_main_menu() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FP);

    padprintln("MAVAI - MyKey/SRIX4K Tool v1.0");
    padprintln("");
    padprintln("Features:");
    padprintln("- Read/Clone SRIX4K (512B)");
    padprintln("- MyKey credit management");
    padprintln("- Vendor read/write");
    padprintln("- Save/Load .srix dumps");
    padprintln("- Read 8-byte UID");
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to open menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _screen_drawn = true;
}

// ==================== TAG WAIT ====================

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

    if (message_shown && millis() - lastDotTime > MAVAI_TAG_TIMEOUT_MS) {
        tft.print(".");
        lastDotTime = millis();
    }

    unsigned long elapsed = millis() - attemptStart;
    if (elapsed > MAVAI_TAG_TIMEOUT_MS) {
        attempts++;
        attemptStart = millis();
        if (attempts >= MAVAI_TAG_MAX_ATTEMPTS) {
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

// ==================== READ TAG ====================

void MAVAITool::read_tag() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    if (!waitForTag()) return;

    display_banner();

    _dump_valid_from_read = false;
    _dump_modified = false;
    srixFlagClear();
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

    for (uint8_t b = 0; b < 128; b++) {
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

        progressHandler(b + 1, 128, "Reading data blocks");
    }

    _dump_valid_from_read = true;
    _dump_valid_from_load = false;

    // Calculate encryption key from UID
    calculateEncryptionKey();

    padprintln("");
    padprintln("");
    displaySuccess("Tag read successfully!");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] for Main Menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _tag_read = true;
    _lastReadTime = millis();
    _screen_drawn = true;
}

// ==================== WRITE TAG ====================

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

    uint8_t blocks_written = 0;
    uint8_t blocks_failed = 0;
    String failed_blocks = "";
    uint8_t block[4];

    // If there are modified blocks, write only those; otherwise write all
    bool has_flags = false;
    for (uint8_t i = 0; i < 4; i++) {
        if (_blockFlags.memory[i] != 0) { has_flags = true; break; }
    }

    if (has_flags && _dump_modified) {
        padprintln("Writing modified blocks...");
        padprint("Please Wait");

        for (uint8_t b = 0; b < 128; b++) {
            if (!srixFlagGet(b)) continue;

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
                delay(MAVAI_EEPROM_WRITE_DELAY_MS);
            }
        }
    } else {
        padprintln("Writing 128 blocks...");
        padprint("Please Wait");

        for (uint8_t b = 0; b < 128; b++) {
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
                delay(MAVAI_EEPROM_WRITE_DELAY_MS);
            }

            progressHandler(b + 1, 128, "Writing data blocks");
        }
    }

    padprintln("");
    padprintln("");

    if (blocks_failed == 0) {
        displaySuccess("Write complete!", true);
        _dump_modified = false;
        srixFlagClear();
    } else if (blocks_written > 0) {
        displayWarning("Partial write!", true);
        padprintln("Written: " + String(blocks_written));
        padprintln("Failed: " + String(blocks_failed));
        if (blocks_failed > 10) {
            padprintln("Failed: " + failed_blocks + "...");
        } else {
            padprintln("Failed: " + failed_blocks);
        }
    } else {
        displayError("Write failed!", true);
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    _lastReadTime = millis();
    delayWithReturn(3000);
    set_state(IDLE_MODE);
}

// ==================== READ UID ====================

void MAVAITool::read_uid() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    if (!waitForTag()) return;

    display_banner();
    padprintln("Tag detected!");
    padprintln("");

    if (!nfc->SRIX_get_uid(_uid)) {
        displayError("Failed to read UID!");
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
        if (i == 1 || i == 3 || i == 5) uid_line += " ";
    }
    uid_line.toUpperCase();
    padprintln(uid_line);
    padprintln("");
    tft.setTextSize(FP);

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] for Main Menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _lastReadTime = millis();
    _screen_drawn = true;
}

// ==================== PN532 INFO ====================

void MAVAITool::show_pn_info() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    uint32_t ver = nfc->getFirmwareVersion();
    if (!ver) {
        displayError("Failed to read firmware!");
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
    padprintln("FW: " + String(fw_major) + "." + String(fw_minor));
    padprintln("");
    padprintln(_has_hardware_pins ? "Mode: Hardware (IRQ+RST)" : "Mode: I2C-Only (Polling)");

    _screen_drawn = true;
}

// ==================== SAVE FILE ====================

void MAVAITool::save_file() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No data in memory!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    String uid_str = "";
    for (uint8_t i = 0; i < 8; i++) {
        if (_uid[i] < 0x10) uid_str += "0";
        uid_str += String(_uid[i], HEX);
    }
    uid_str.toUpperCase();

    String filename = keyboard(uid_str, 30, "File name:");
    filename.trim();

    if (filename == "\x1B" || filename.isEmpty()) {
        delay(500);
        set_state(IDLE_MODE);
        return;
    }

    display_banner();

    FS *fs;
    if (!getFsStorage(fs)) {
        displayError("Filesystem error!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    if (!(*fs).exists("/BruceRFID")) (*fs).mkdir("/BruceRFID");
    if (!(*fs).exists("/BruceRFID/SRIX")) (*fs).mkdir("/BruceRFID/SRIX");

    String filepath = "/BruceRFID/SRIX/" + filename;
    if ((*fs).exists(filepath + ".srix")) {
        int i = 1;
        while ((*fs).exists(filepath + "_" + String(i) + ".srix")) i++;
        filepath += "_" + String(i);
    }
    filepath += ".srix";

    if (!save_file_data(fs, filepath)) {
        displayError("Error saving file!");
        delay(1500);
        set_state(IDLE_MODE);
        return;
    }

    displaySuccess("File saved!");
    padprintln("Path: " + filepath);
    delayWithReturn(2500);
    set_state(IDLE_MODE);
}

bool MAVAITool::save_file_data(FS *fs, String filepath) {
    File file = (*fs).open(filepath, FILE_WRITE);
    if (!file) return false;

    String uid_str = "";
    for (uint8_t i = 0; i < 8; i++) {
        if (_uid[i] < 0x10) uid_str += "0";
        uid_str += String(_uid[i], HEX);
    }
    uid_str.toUpperCase();

    file.println("Filetype: Bruce SRIX Dump");
    file.println("Version: MAVAI-1.0");
    file.println("UID: " + uid_str);
    file.println("Blocks: 128");
    file.println("Data size: 512");
    file.println("# Data:");

    for (uint8_t block = 0; block < 128; block++) {
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
    return true;
}

// ==================== LOAD FILE ====================

void MAVAITool::load_file() {
    FS *fs;
    if (!getFsStorage(fs)) {
        displayError("Filesystem error!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    if (!(*fs).exists("/BruceRFID/SRIX")) {
        displayError("No dumps found!");
        displayError("Folder /BruceRFID/SRIX missing");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    File dir = (*fs).open("/BruceRFID/SRIX");
    if (!dir || !dir.isDirectory()) {
        displayError("Cannot open SRIX folder!");
        delay(1500);
        set_state(IDLE_MODE);
        return;
    }

    std::vector<String> fileList;
    File file = dir.openNextFile();
    while (file) {
        String filename = String(file.name());
        if (filename.endsWith(".srix") && !file.isDirectory()) {
            int lastSlash = filename.lastIndexOf('/');
            if (lastSlash >= 0) filename = filename.substring(lastSlash + 1);
            fileList.push_back(filename);
        }
        file = dir.openNextFile();
    }
    dir.close();

    if (fileList.empty()) {
        displayError("No .srix files found!");
        delay(2500);
        set_state(IDLE_MODE);
        return;
    }

    display_banner();
    padprintln("Select file to load:");
    padprintln("");

    options = {};
    for (const String &fname : fileList) {
        options.emplace_back(fname, [this, fname, fs]() {
            load_file_data(fs, "/BruceRFID/SRIX/" + fname);
        });
    }
    options.emplace_back("Cancel", [this]() { set_state(IDLE_MODE); });
    loopOptions(options);
}

void MAVAITool::load_file_data(FS *fs, String filepath) {
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

    memset(_dump, 0, sizeof(_dump));
    memset(_uid, 0, sizeof(_uid));
    _dump_valid_from_read = false;
    _dump_modified = false;
    srixFlagClear();

    bool header_passed = false;
    int blocks_loaded = 0;
    String uid_from_file = "";

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;

        if (line.startsWith("UID:")) {
            uid_from_file = line.substring(5);
            uid_from_file.trim();
            uid_from_file.replace(" ", "");
            for (uint8_t i = 0; i < 8 && i * 2 < (int)uid_from_file.length(); i++) {
                String byteStr = uid_from_file.substring(i * 2, i * 2 + 2);
                _uid[i] = strtoul(byteStr.c_str(), NULL, 16);
            }
            continue;
        }

        if (!header_passed) {
            if (line.startsWith("# Data:")) header_passed = true;
            continue;
        }

        if (line.startsWith("[") && line.indexOf("]") > 0) {
            int bracket_end = line.indexOf("]");
            String block_num_str = line.substring(1, bracket_end);
            String data_str = line.substring(bracket_end + 1);
            data_str.trim();
            data_str.replace(" ", "");

            uint8_t block_num = strtoul(block_num_str.c_str(), NULL, 16);
            if (block_num >= 128) continue;

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

    if (blocks_loaded < 128) {
        displayError("Incomplete dump!");
        displayError("Loaded " + String(blocks_loaded) + "/128 blocks");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    _dump_valid_from_load = true;
    _dump_valid_from_read = false;

    // Calculate encryption key from loaded UID
    calculateEncryptionKey();

    displaySuccess("Dump loaded!");
    delay(1000);

    String filename = filepath;
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) filename = filename.substring(lastSlash + 1);

    current_state = LOAD_MODE;
    _screen_drawn = false;
    display_banner();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("File: " + filename);
    padprintln("UID: " + uid_from_file);
    padprintln("Blocks: " + String(blocks_loaded));
    tft.setTextSize(FP);
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to open menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    _lastReadTime = millis();
    _screen_drawn = true;
}

// ==================== MYKEY INFO ====================

void MAVAITool::show_mykey_info() {
    if (_screen_drawn) {
        delay(50);
        return;
    }

    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        displayError("No dump in memory!");
        delay(2000);
        set_state(IDLE_MODE);
        return;
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("MyKey Info:");
    tft.setTextSize(FP);
    padprintln("");

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

    // Show credit
    uint16_t credit = getCurrentCredit();
    uint16_t prev_credit = getPreviousCredit();
    padprintln("Credit: " + formatCreditEUR(credit));
    padprintln("Prev:   " + formatCreditEUR(prev_credit));
    padprintln("");

    // Show key ID
    uint32_t key_id = getKeyID();
    padprintln("Key ID: " + toHex32(key_id));
    padprintln("Date:   " + getProductionDate());
    padprintln("");

    // Show reset state
    padprintln(isReset() ? "State: RESET" : "State: ACTIVE");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("");
    padprintln("Press [OK] for menu");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    _screen_drawn = true;
}

// ==================== BLOCK FLAG OPERATIONS ====================

void MAVAITool::srixFlagClear() {
    memset(&_blockFlags, 0, sizeof(_blockFlags));
}

void MAVAITool::srixFlagAdd(uint8_t blockNum) {
    if (blockNum >= 128) return;
    uint8_t word = blockNum / 32;
    uint8_t bit = blockNum % 32;
    _blockFlags.memory[word] |= (1UL << bit);
}

bool MAVAITool::srixFlagGet(uint8_t blockNum) {
    if (blockNum >= 128) return false;
    uint8_t word = blockNum / 32;
    uint8_t bit = blockNum % 32;
    return (_blockFlags.memory[word] & (1UL << bit)) != 0;
}

// ==================== BLOCK TYPE ====================

uint8_t MAVAITool::getBlockType(uint8_t blockNum) {
    // SRIX4K memory map:
    // Block 0-4:   Resettable OTP (7 blocks total from SRIX4K spec)
    // Block 5:     Counter
    // Block 6:     OTP (MyKey specific)
    // Block 7-15:  Lockable EEPROM
    // Block 16-127: Standard EEPROM
    if (blockNum <= 4) return SRIX_BLOCK_TYPE_RESETTABLE_OTP;
    if (blockNum == 5) return SRIX_BLOCK_TYPE_COUNTER;
    if (blockNum == 6) return SRIX_BLOCK_TYPE_LOCKABLE;
    if (blockNum <= 15) return SRIX_BLOCK_TYPE_LOCKABLE;
    return SRIX_BLOCK_TYPE_EEPROM;
}

bool MAVAITool::isBlockWritable(uint8_t blockNum) {
    // Blocks 0-4 are resettable OTP - write carefully
    // All other blocks are freely writable for cloning
    return true;
}

// ==================== BLOCK OPERATIONS ====================

uint32_t* MAVAITool::getBlockPtr(uint8_t blockNum) {
    if (blockNum >= 128) return nullptr;
    return (uint32_t*)(&_dump[blockNum * 4]);
}

uint32_t MAVAITool::readBlockAsUint32(uint8_t blockNum) {
    if (blockNum >= 128) return 0;
    uint16_t off = blockNum * 4;
    uint32_t val = ((uint32_t)_dump[off + 0] << 24) |
                   ((uint32_t)_dump[off + 1] << 16) |
                   ((uint32_t)_dump[off + 2] << 8)  |
                   ((uint32_t)_dump[off + 3]);
    return val;
}

void MAVAITool::writeBlockAsUint32(uint8_t blockNum, uint32_t value) {
    writeBlockToMemory(blockNum, value);
    srixFlagAdd(blockNum);
    _dump_modified = true;
}

void MAVAITool::writeBlockToMemory(uint8_t blockNum, uint32_t value) {
    if (blockNum >= 128) return;
    uint16_t off = blockNum * 4;
    _dump[off + 0] = (value >> 24) & 0xFF;
    _dump[off + 1] = (value >> 16) & 0xFF;
    _dump[off + 2] = (value >> 8) & 0xFF;
    _dump[off + 3] = (value) & 0xFF;
}

void MAVAITool::modifyBlock(uint8_t blockNum, uint8_t *data) {
    if (blockNum >= 128) return;
    uint16_t off = blockNum * 4;
    memcpy(&_dump[off], data, 4);
    srixFlagAdd(blockNum);
    _dump_modified = true;
}

uint8_t MAVAITool::writeModifiedBlocksToTag() {
    uint8_t written = 0;
    uint8_t block[4];

    for (uint8_t b = 0; b < 128; b++) {
        if (!srixFlagGet(b)) continue;
        const uint16_t off = (uint16_t)b * 4;
        block[0] = _dump[off + 0];
        block[1] = _dump[off + 1];
        block[2] = _dump[off + 2];
        block[3] = _dump[off + 3];
        if (nfc->SRIX_write_block(b, block)) {
            written++;
            delay(MAVAI_EEPROM_WRITE_DELAY_MS);
        }
    }
    return written;
}

bool MAVAITool::validateManufacturerCode() {
    // SRIX4K UID byte 0 should be 0xD0 (ST Microelectronics)
    // and byte 1 should be 0x02
    return (_uid[0] == SRIX_MANUFACTURER_BYTE_MSB && _uid[1] == SRIX_MANUFACTURER_BYTE_LSB);
}

// ==================== MYKEY ENCRYPTION ====================

uint32_t MAVAITool::byteSwap32(uint32_t value) {
    return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8)  |
           ((value & 0x0000FF00) << 8)  |
           ((value & 0x000000FF) << 24);
}

uint64_t MAVAITool::getUidAsUint64() {
    uint64_t uid64 = 0;
    for (int i = 0; i < 8; i++) {
        uid64 = (uid64 << 8) | _uid[i];
    }
    return uid64;
}

uint32_t MAVAITool::getUidForEncryption() {
    // Use bytes 2-5 of the UID for encryption (chip serial number part)
    return ((uint32_t)_uid[2] << 24) |
           ((uint32_t)_uid[3] << 16) |
           ((uint32_t)_uid[4] << 8)  |
           ((uint32_t)_uid[5]);
}

void MAVAITool::calculateEncryptionKey() {
    // Derive encryption key from UID
    // This is a simplified key derivation - the actual MyKey algorithm is proprietary
    uint32_t uid_part = getUidForEncryption();
    _encryptionKey = uid_part ^ 0xA5A5A5A5UL;
    _encryptionKey = byteSwap32(_encryptionKey) ^ uid_part;
}

void MAVAITool::encodeDecodeBlock(uint32_t *block) {
    // XOR-based symmetric encode/decode
    if (block) {
        *block ^= _encryptionKey;
    }
}

uint32_t MAVAITool::calculateOTP(uint32_t otpBlock) {
    // Calculate OTP value - simplified implementation
    return otpBlock ^ _encryptionKey;
}

// ==================== MYKEY DATA ====================

uint16_t MAVAITool::getCurrentCredit() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;

    // Credit is stored in blocks 21-22
    // Block 21 (lower 16 bits of credit in cents) + block 22 (high word / CRC)
    uint32_t block21 = readBlockAsUint32(MYKEY_BLOCK_CREDIT1);
    // Decode: lower 16 bits represent the credit in cents
    uint16_t credit = (uint16_t)(block21 & 0xFFFF);
    return credit;
}

uint16_t MAVAITool::getPreviousCredit() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return 0;

    uint32_t block23 = readBlockAsUint32(MYKEY_BLOCK_PREVCREDIT1);
    uint16_t credit = (uint16_t)(block23 & 0xFFFF);
    return credit;
}

bool MAVAITool::setCredit(uint16_t cents) {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;

    // Save current credit as previous
    uint32_t current = readBlockAsUint32(MYKEY_BLOCK_CREDIT1);
    uint32_t current2 = readBlockAsUint32(MYKEY_BLOCK_CREDIT2);
    writeBlockAsUint32(MYKEY_BLOCK_PREVCREDIT1, current);
    writeBlockAsUint32(MYKEY_BLOCK_PREVCREDIT2, current2);

    // Set new credit (lower 16 bits = cents, upper 16 bits = checksum)
    uint32_t newBlock1 = (uint32_t)cents;
    uint32_t newBlock2 = ~newBlock1; // Simple complement checksum
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT1, newBlock1);
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT2, newBlock2);

    return true;
}

bool MAVAITool::addCredit(uint16_t cents) {
    uint16_t current = getCurrentCredit();
    uint32_t newCredit = (uint32_t)current + (uint32_t)cents;
    if (newCredit > 0xFFFF) newCredit = 0xFFFF; // Cap at max
    return setCredit((uint16_t)newCredit);
}

void MAVAITool::exportVendor(uint32_t *vendor) {
    if (!vendor) return;
    vendor[0] = readBlockAsUint32(MYKEY_BLOCK_VENDOR1);
    vendor[1] = readBlockAsUint32(MYKEY_BLOCK_VENDOR2);
}

void MAVAITool::importVendor(uint32_t vendor) {
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR1, vendor);
    writeBlockAsUint32(MYKEY_BLOCK_VENDOR2, ~vendor);
    _currentVendor = vendor;
    _vendorCalculated = true;
}

bool MAVAITool::resetKey() {
    if (!_dump_valid_from_read && !_dump_valid_from_load) return false;
    // Reset: zero out credit blocks
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT1, 0x00000000UL);
    writeBlockAsUint32(MYKEY_BLOCK_CREDIT2, 0xFFFFFFFFUL);
    writeBlockAsUint32(MYKEY_BLOCK_PREVCREDIT1, 0x00000000UL);
    writeBlockAsUint32(MYKEY_BLOCK_PREVCREDIT2, 0xFFFFFFFFUL);
    return true;
}

bool MAVAITool::isReset() {
    uint16_t credit = getCurrentCredit();
    return (credit == 0);
}

uint32_t MAVAITool::getKeyID() {
    // Key ID is typically in block 0 or derived from UID
    return readBlockAsUint32(0);
}

String MAVAITool::getProductionDate() {
    // Production date may be encoded in vendor blocks or UID
    // UID bytes 2-4 typically contain chip serial including production date for ST chips
    // Return as placeholder from UID
    char buf[16];
    snprintf(buf, sizeof(buf), "%02X%02X-%02X",
             _uid[2], _uid[3], _uid[4]);
    return String(buf);
}

void MAVAITool::restoreDumpFromBackup(const uint8_t* backupDump, const uint8_t* backupUid) {
    if (backupDump) memcpy(_dump, backupDump, SRIX4K_BYTES);
    if (backupUid) memcpy(_uid, backupUid, SRIX_UID_LENGTH);
    _dump_modified = false;
    srixFlagClear();
}

// ==================== SERIAL OPERATIONS ====================

bool MAVAITool::read_tag_serial() {
    if (!nfc) return false;

    // Try to find tag
    if (!nfc->SRIX_initiate_select()) return false;

    // Read UID
    if (!nfc->SRIX_get_uid(_uid)) return false;

    // Read all 128 blocks
    uint8_t block[4];
    for (uint8_t b = 0; b < 128; b++) {
        if (!nfc->SRIX_read_block(b, block)) return false;
        const uint16_t off = (uint16_t)b * 4;
        _dump[off + 0] = block[0];
        _dump[off + 1] = block[1];
        _dump[off + 2] = block[2];
        _dump[off + 3] = block[3];
    }

    _dump_valid_from_read = true;
    _dump_valid_from_load = false;
    _dump_modified = false;
    srixFlagClear();
    calculateEncryptionKey();
    return true;
}

bool MAVAITool::write_tag_serial() {
    if (!nfc || (!_dump_valid_from_read && !_dump_valid_from_load)) return false;
    if (!nfc->SRIX_initiate_select()) return false;

    uint8_t block[4];
    for (uint8_t b = 0; b < 128; b++) {
        const uint16_t off = (uint16_t)b * 4;
        block[0] = _dump[off + 0];
        block[1] = _dump[off + 1];
        block[2] = _dump[off + 2];
        block[3] = _dump[off + 3];
        if (!nfc->SRIX_write_block(b, block)) return false;
        delay(MAVAI_EEPROM_WRITE_DELAY_MS);
    }
    return true;
}

// ==================== FORMATTING HELPERS ====================

String MAVAITool::toHex32(uint32_t value) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lX", (unsigned long)value);
    return String(buf);
}

String MAVAITool::formatCreditEUR(uint16_t cents) {
    uint16_t euros = cents / 100;
    uint16_t c = cents % 100;
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%02u EUR", euros, c);
    return String(buf);
}

// ==================== CREDIT UI ====================

void MAVAITool::view_credit_ui() {
    display_banner();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("Credit Info:");
    tft.setTextSize(FP);
    padprintln("");
    uint16_t current = getCurrentCredit();
    uint16_t previous = getPreviousCredit();
    padprintln("Current:  " + formatCreditEUR(current));
    padprintln("Previous: " + formatCreditEUR(previous));
    padprintln("");
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to continue");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    delayWithReturn(4000);
    set_state(MYKEY_INFO_MODE);
}

void MAVAITool::add_credit_ui(uint16_t amount) {
    display_banner();

    uint16_t current = getCurrentCredit();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("Current: " + formatCreditEUR(current));
    padprintln("");

    String input;
    if (amount == 0) {
        input = keyboard("100", 8, "Add cents:");
        if (input == "\x1B" || input.isEmpty()) {
            set_state(MYKEY_INFO_MODE);
            return;
        }
        amount = (uint16_t)input.toInt();
    }

    if (!addCredit(amount)) {
        displayError("Failed to add credit!");
        delay(2000);
    } else {
        displaySuccess("Credit updated!");
        padprintln("New: " + formatCreditEUR(getCurrentCredit()));
        delay(2000);
    }
    set_state(MYKEY_INFO_MODE);
}

void MAVAITool::sub_credit_ui(uint16_t amount) {
    display_banner();

    uint16_t current = getCurrentCredit();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("Current: " + formatCreditEUR(current));
    padprintln("");

    String input = keyboard("100", 8, "Sub cents:");
    if (input == "\x1B" || input.isEmpty()) {
        set_state(MYKEY_INFO_MODE);
        return;
    }
    uint16_t sub = (uint16_t)input.toInt();
    uint16_t newCredit = (sub > current) ? 0 : current - sub;

    if (!setCredit(newCredit)) {
        displayError("Failed!");
        delay(2000);
    } else {
        displaySuccess("Credit updated!");
        padprintln("New: " + formatCreditEUR(newCredit));
        delay(2000);
    }
    set_state(MYKEY_INFO_MODE);
}

void MAVAITool::set_credit_ui() {
    display_banner();

    uint16_t current = getCurrentCredit();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("Current: " + formatCreditEUR(current));
    padprintln("");

    String input = keyboard(String(current), 8, "Set cents:");
    if (input == "\x1B" || input.isEmpty()) {
        set_state(MYKEY_INFO_MODE);
        return;
    }
    uint16_t newCredit = (uint16_t)input.toInt();

    if (!setCredit(newCredit)) {
        displayError("Failed!");
        delay(2000);
    } else {
        displaySuccess("Credit set!");
        padprintln("New: " + formatCreditEUR(newCredit));
        delay(2000);
    }
    set_state(MYKEY_INFO_MODE);
}

// ==================== VENDOR UI ====================

void MAVAITool::view_vendor_ui() {
    display_banner();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    padprintln("Vendor Info:");
    tft.setTextSize(FP);
    padprintln("");

    uint32_t vendor[2];
    exportVendor(vendor);
    padprintln("Vendor1: " + toHex32(vendor[0]));
    padprintln("Vendor2: " + toHex32(vendor[1]));
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to continue");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    delayWithReturn(4000);
    set_state(MYKEY_INFO_MODE);
}

void MAVAITool::import_vendor_ui() {
    display_banner();

    uint32_t vendor[2];
    exportVendor(vendor);
    padprintln("Current V1: " + toHex32(vendor[0]));
    padprintln("");

    String input = keyboard(toHex32(vendor[0]), 8, "Vendor hex:");
    if (input == "\x1B" || input.isEmpty()) {
        set_state(MYKEY_INFO_MODE);
        return;
    }

    uint32_t newVendor = strtoul(input.c_str(), NULL, 16);
    importVendor(newVendor);
    displaySuccess("Vendor updated!");
    delay(2000);
    set_state(MYKEY_INFO_MODE);
}

void MAVAITool::export_vendor_ui() {
    display_banner();
    uint32_t vendor[2];
    exportVendor(vendor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("Vendor1: " + toHex32(vendor[0]));
    padprintln("Vendor2: " + toHex32(vendor[1]));
    padprintln("");
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [OK] to continue");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    delayWithReturn(3000);
    set_state(MYKEY_INFO_MODE);
}

// ==================== RESET UI ====================

void MAVAITool::reset_key_ui() {
    display_banner();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("Reset MyKey?");
    padprintln("This will zero credit!");
    padprintln("");
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("[OK] = Confirm");
    padprintln("[ESC] = Cancel");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    // Wait for confirm or cancel
    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (check(EscPress)) {
            set_state(MYKEY_INFO_MODE);
            return;
        }
        if (check(SelPress)) {
            if (resetKey()) {
                displaySuccess("Key reset!");
            } else {
                displayError("Reset failed!");
            }
            delay(2000);
            set_state(MYKEY_INFO_MODE);
            return;
        }
        delay(50);
    }
    set_state(MYKEY_INFO_MODE);
}

// ==================== ENTRY POINT ====================

void MAVAI_Tool() { MAVAITool mavai_tool; }

/**
 * @file mavai_serial_menu.cpp
 * @brief MAVAI Serial Interactive Menu for MyKey/SRIX4K operations
 * @date 2026-01-17
 */

#include "mavai_serial_menu.h"
#include "core/sd_functions.h"
#include <globals.h>

// ==================== CONSTRUCTOR / DESTRUCTOR ====================

MAVAISerialMenu::MAVAISerialMenu() {
    tool = nullptr;
}

MAVAISerialMenu::~MAVAISerialMenu() {
    delete tool;
}

// ==================== MAIN RUN LOOP ====================

void MAVAISerialMenu::run() {
    serialDevice->println(F(""));
    serialDevice->println(F("========================================"));
    serialDevice->println(F("  MAVAI - MyKey/SRIX4K Serial Manager"));
    serialDevice->println(F("  Type 'help' for available commands"));
    serialDevice->println(F("========================================"));
    serialDevice->println(F(""));

    // Initialize hardware
    int sda_pin = bruceConfigPins.i2c_bus.sda;
    int scl_pin = bruceConfigPins.i2c_bus.scl;
    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(100000);

    tool = new MAVAITool(true /* headless */);
    if (!tool || !tool->initHardware()) {
        serialDevice->println(F("ERROR: Failed to initialize PN532 hardware!"));
        serialDevice->println(F("Check wiring and I2C address."));
        delete tool;
        tool = nullptr;
        return;
    }

    serialDevice->println(F("PN532 initialized OK"));
    serialDevice->println(F("Ready. Type 'help' for commands."));
    serialDevice->println(F(""));

    while (true) {
        serialDevice->print(F("mavai> "));

        String line = readLine(60000);
        line.trim();

        if (line.isEmpty()) continue;

        // Parse command
        int spaceIdx = line.indexOf(' ');
        String cmd = (spaceIdx > 0) ? line.substring(0, spaceIdx) : line;
        String arg = (spaceIdx > 0) ? line.substring(spaceIdx + 1) : "";
        arg.trim();
        cmd.toLowerCase();

        if (cmd == "help" || cmd == "?") {
            printHelp();
        } else if (cmd == "status") {
            printStatus();
        } else if (cmd == "read") {
            uint32_t timeout = arg.isEmpty() ? 10000 : (uint32_t)arg.toInt() * 1000;
            serialDevice->println(F("Waiting for tag... (place tag on reader)"));
            if (cmdRead(timeout)) {
                printOK("Tag read successfully");
                printUID();
            } else {
                printError("Failed to read tag (timeout or error)");
            }
        } else if (cmd == "write") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory. Use 'read' or 'load' first.");
                continue;
            }
            uint32_t timeout = arg.isEmpty() ? 10000 : (uint32_t)arg.toInt() * 1000;
            serialDevice->println(F("Waiting for tag... (place tag on reader)"));
            if (cmdWrite(timeout)) {
                printOK("Tag written successfully");
            } else {
                printError("Failed to write tag");
            }
        } else if (cmd == "dump") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory. Use 'read' or 'load' first.");
            } else {
                printDump();
            }
        } else if (cmd == "uid") {
            printUID();
        } else if (cmd == "credit") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory.");
            } else {
                printCredit();
            }
        } else if (cmd == "vendor") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory.");
            } else {
                printVendor();
            }
        } else if (cmd == "info") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory. Use 'read' or 'load' first.");
            } else {
                printMyKeyInfo();
            }
        } else if (cmd == "save") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory.");
                continue;
            }
            if (arg.isEmpty()) {
                // Generate filename from UID
                const uint8_t* uid = tool->getUID();
                char uidStr[17];
                snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X%02X",
                         uid[0], uid[1], uid[2], uid[3],
                         uid[4], uid[5], uid[6], uid[7]);
                arg = String(uidStr);
            }
            if (cmdSave(arg)) {
                printOK("Saved to /BruceRFID/SRIX/" + arg + ".srix");
            } else {
                printError("Failed to save file");
            }
        } else if (cmd == "load") {
            if (arg.isEmpty()) {
                printError("Usage: load <filename>");
                continue;
            }
            if (!arg.endsWith(".srix")) arg += ".srix";
            if (cmdLoad("/BruceRFID/SRIX/" + arg)) {
                printOK("Loaded " + arg);
                printUID();
            } else {
                printError("Failed to load file: " + arg);
            }
        } else if (cmd == "setcredit") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory.");
                continue;
            }
            if (arg.isEmpty()) {
                printError("Usage: setcredit <cents>");
                continue;
            }
            uint16_t cents = (uint16_t)arg.toInt();
            if (cmdSetCredit(cents)) {
                printOK("Credit set to " + String(cents) + " cents");
            } else {
                printError("Failed to set credit");
            }
        } else if (cmd == "addcredit") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory.");
                continue;
            }
            if (arg.isEmpty()) {
                printError("Usage: addcredit <cents>");
                continue;
            }
            uint16_t cents = (uint16_t)arg.toInt();
            if (cmdAddCredit(cents)) {
                printOK("Added " + String(cents) + " cents");
            } else {
                printError("Failed to add credit");
            }
        } else if (cmd == "setvendor") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory.");
                continue;
            }
            if (arg.isEmpty()) {
                printError("Usage: setvendor <hex32>");
                continue;
            }
            uint32_t vendor = strtoul(arg.c_str(), NULL, 16);
            if (cmdSetVendor(vendor)) {
                printOK("Vendor set");
            } else {
                printError("Failed to set vendor");
            }
        } else if (cmd == "reset") {
            if (!tool->isDumpValidFromRead() && !tool->isDumpValidFromLoad()) {
                printError("No dump in memory.");
                continue;
            }
            serialDevice->print(F("Confirm reset? (y/n): "));
            String confirm = readLine(10000);
            confirm.trim();
            confirm.toLowerCase();
            if (confirm == "y" || confirm == "yes") {
                if (cmdReset()) {
                    printOK("Key reset (credit zeroed in memory)");
                } else {
                    printError("Reset failed");
                }
            } else {
                serialDevice->println(F("Reset cancelled."));
            }
        } else if (cmd == "exit" || cmd == "quit" || cmd == "q") {
            serialDevice->println(F("Exiting MAVAI Serial Manager."));
            break;
        } else {
            printError("Unknown command: " + cmd + ". Type 'help' for help.");
        }
    }
}

// ==================== COMMAND IMPLEMENTATIONS ====================

bool MAVAISerialMenu::cmdRead(uint32_t timeout_ms) {
    if (!tool) return false;
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        if (tool->read_tag_serial()) return true;
        delay(200);
    }
    return false;
}

bool MAVAISerialMenu::cmdWrite(uint32_t timeout_ms) {
    if (!tool) return false;
    // Initialize tag first
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        if (tool->write_tag_serial()) return true;
        delay(200);
    }
    return false;
}

bool MAVAISerialMenu::cmdSave(const String &filename) {
    if (!tool) return false;
    FS *fs;
    if (!getFsStorage(fs)) return false;
    if (!(*fs).exists("/BruceRFID")) (*fs).mkdir("/BruceRFID");
    if (!(*fs).exists("/BruceRFID/SRIX")) (*fs).mkdir("/BruceRFID/SRIX");
    String filepath = "/BruceRFID/SRIX/" + filename;
    if (!filepath.endsWith(".srix")) filepath += ".srix";
    return tool->save_file_data(fs, filepath);
}

bool MAVAISerialMenu::cmdLoad(const String &filepath) {
    if (!tool) return false;
    FS *fs;
    if (!getFsStorage(fs)) return false;
    if (!(*fs).exists(filepath)) return false;
    tool->load_file_data(fs, filepath);
    return tool->isDumpValidFromLoad();
}

bool MAVAISerialMenu::cmdSetCredit(uint16_t cents) {
    if (!tool) return false;
    return tool->setCredit(cents);
}

bool MAVAISerialMenu::cmdAddCredit(uint16_t cents) {
    if (!tool) return false;
    return tool->addCredit(cents);
}

bool MAVAISerialMenu::cmdSetVendor(uint32_t vendor) {
    if (!tool) return false;
    tool->importVendor(vendor);
    return true;
}

bool MAVAISerialMenu::cmdReset() {
    if (!tool) return false;
    return tool->resetKey();
}

// ==================== PRINT HELPERS ====================

void MAVAISerialMenu::printHelp() {
    serialDevice->println(F(""));
    serialDevice->println(F("MAVAI Commands:"));
    serialDevice->println(F("  read [timeout_s]     - Read tag (default 10s timeout)"));
    serialDevice->println(F("  write [timeout_s]    - Write dump to tag"));
    serialDevice->println(F("  dump                 - Print hex dump of all 128 blocks"));
    serialDevice->println(F("  uid                  - Print tag UID"));
    serialDevice->println(F("  credit               - Show current/previous credit"));
    serialDevice->println(F("  vendor               - Show vendor blocks"));
    serialDevice->println(F("  info                 - Show MyKey full info"));
    serialDevice->println(F("  save [filename]      - Save dump to SD/LittleFS"));
    serialDevice->println(F("  load <filename>      - Load dump from SD/LittleFS"));
    serialDevice->println(F("  setcredit <cents>    - Set credit value (in cents)"));
    serialDevice->println(F("  addcredit <cents>    - Add to current credit"));
    serialDevice->println(F("  setvendor <hex32>    - Set vendor code (8 hex chars)"));
    serialDevice->println(F("  reset                - Reset key (zero credit in memory)"));
    serialDevice->println(F("  status               - Show dump/memory status"));
    serialDevice->println(F("  help                 - Show this help"));
    serialDevice->println(F("  exit                 - Exit MAVAI serial manager"));
    serialDevice->println(F(""));
}

void MAVAISerialMenu::printStatus() {
    serialDevice->println(F(""));
    serialDevice->println(F("--- Status ---"));
    if (!tool) {
        serialDevice->println(F("Hardware: NOT INITIALIZED"));
        return;
    }
    serialDevice->println(F("Hardware: OK"));
    serialDevice->print(F("Dump from read: "));
    serialDevice->println(tool->isDumpValidFromRead() ? F("YES") : F("NO"));
    serialDevice->print(F("Dump from load: "));
    serialDevice->println(tool->isDumpValidFromLoad() ? F("YES") : F("NO"));
    serialDevice->print(F("Dump modified:  "));
    serialDevice->println(tool->isDumpModified() ? F("YES") : F("NO"));
    serialDevice->println(F(""));
}

void MAVAISerialMenu::printUID() {
    if (!tool) return;
    const uint8_t* uid = tool->getUID();
    serialDevice->print(F("UID: "));
    for (int i = 0; i < 8; i++) {
        if (uid[i] < 0x10) serialDevice->print('0');
        serialDevice->print(uid[i], HEX);
        if (i < 7) serialDevice->print(' ');
    }
    serialDevice->println(F(""));
}

void MAVAISerialMenu::printDump() {
    if (!tool) return;
    const uint8_t* dump = tool->getDump();
    serialDevice->println(F(""));
    serialDevice->println(F("--- Dump (128 blocks x 4 bytes) ---"));
    for (uint8_t b = 0; b < 128; b++) {
        const uint16_t off = (uint16_t)b * 4;
        char line[24];
        snprintf(line, sizeof(line), "[%02X] %02X%02X%02X%02X",
                 b, dump[off], dump[off+1], dump[off+2], dump[off+3]);
        serialDevice->println(line);
    }
    serialDevice->println(F(""));
}

void MAVAISerialMenu::printCredit() {
    if (!tool) return;
    uint16_t current = tool->getCurrentCredit();
    uint16_t previous = tool->getPreviousCredit();
    uint16_t c_eur = current / 100;
    uint16_t c_cents = current % 100;
    uint16_t p_eur = previous / 100;
    uint16_t p_cents = previous % 100;

    serialDevice->println(F(""));
    serialDevice->println(F("--- Credit ---"));
    char buf[32];
    snprintf(buf, sizeof(buf), "Current:  %u.%02u EUR (%u cents)", c_eur, c_cents, current);
    serialDevice->println(buf);
    snprintf(buf, sizeof(buf), "Previous: %u.%02u EUR (%u cents)", p_eur, p_cents, previous);
    serialDevice->println(buf);
    serialDevice->println(F(""));
}

void MAVAISerialMenu::printVendor() {
    if (!tool) return;
    uint32_t vendor[2];
    tool->exportVendor(vendor);
    serialDevice->println(F(""));
    serialDevice->println(F("--- Vendor ---"));
    char buf[32];
    snprintf(buf, sizeof(buf), "Block 18: %08lX", (unsigned long)vendor[0]);
    serialDevice->println(buf);
    snprintf(buf, sizeof(buf), "Block 19: %08lX", (unsigned long)vendor[1]);
    serialDevice->println(buf);
    serialDevice->println(F(""));
}

void MAVAISerialMenu::printMyKeyInfo() {
    if (!tool) return;
    printUID();
    printCredit();
    printVendor();

    serialDevice->println(F("--- MyKey ---"));
    char buf[32];
    snprintf(buf, sizeof(buf), "Key ID: %08lX", (unsigned long)tool->getKeyID());
    serialDevice->println(buf);
    serialDevice->print(F("Date:   "));
    serialDevice->println(tool->getProductionDate());
    serialDevice->print(F("State:  "));
    serialDevice->println(tool->isReset() ? F("RESET") : F("ACTIVE"));
    serialDevice->println(F(""));
}

void MAVAISerialMenu::printOK(const String &msg) {
    serialDevice->print(F("OK"));
    if (msg.length() > 0) {
        serialDevice->print(F(": "));
        serialDevice->println(msg);
    } else {
        serialDevice->println(F(""));
    }
}

void MAVAISerialMenu::printError(const String &msg) {
    serialDevice->print(F("ERROR: "));
    serialDevice->println(msg);
}

void MAVAISerialMenu::printSeparator() {
    serialDevice->println(F("----------------------------------------"));
}

// ==================== INPUT HELPER ====================

String MAVAISerialMenu::readLine(uint32_t timeout_ms) {
    String line = "";
    uint32_t start = millis();

    while (true) {
        if ((millis() - start) > timeout_ms) break;

        if (serialDevice->available()) {
            char c = serialDevice->read();
            if (c == '\n' || c == '\r') {
                if (line.length() > 0) break;
            } else {
                line += c;
                serialDevice->print(c); // Echo
            }
        }
        delay(1);
    }
    serialDevice->println(F(""));
    return line;
}

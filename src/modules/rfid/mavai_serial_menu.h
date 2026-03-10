/**
 * @file mavai_serial_menu.h
 * @brief MAVAI Serial Interactive Menu for MyKey/SRIX4K operations
 * @date 2026-01-17
 */

#ifndef __MAVAI_SERIAL_MENU_H__
#define __MAVAI_SERIAL_MENU_H__

#include "mavai.h"
#include <Arduino.h>

class MAVAISerialMenu {
public:
    MAVAISerialMenu();
    ~MAVAISerialMenu();

    void run();

private:
    MAVAITool *tool;

    void printHelp();
    void printStatus();
    void printDump();
    void printUID();
    void printCredit();
    void printVendor();
    void printMyKeyInfo();

    bool cmdRead(uint32_t timeout_ms);
    bool cmdWrite(uint32_t timeout_ms);
    bool cmdSave(const String &filename);
    bool cmdLoad(const String &filename);
    bool cmdSetCredit(uint16_t cents);
    bool cmdAddCredit(uint16_t cents);
    bool cmdSetVendor(uint32_t vendor);
    bool cmdReset();

    String readLine(uint32_t timeout_ms = 30000);
    void printOK(const String &msg = "");
    void printError(const String &msg);
    void printSeparator();
};

#endif

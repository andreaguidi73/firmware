#include "mavai_commands.h"
#include "modules/rfid/mavai_serial_menu.h"
#include <globals.h>

static void mavaiCallback(cmd *c) {
    Command cmd(c);
    serialDevice->println("Starting MAVAI Serial Manager...");
    MAVAISerialMenu menu;
    menu.run();
}

void createMavaiCommands(SimpleCLI *cli) {
    Command mavai = cli->addCommand("mavai", mavaiCallback);
    mavai.setDescription("Launch MAVAI MyKey/SRIX4K interactive serial menu");
}

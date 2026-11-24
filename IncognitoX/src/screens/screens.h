#pragma once

#include "app/app.h"

namespace screens {

void openHome();
void openWifi();
void openBle();
void openSubGhz();
void openNrf();
void openNfc();
void openIr();
void openGps();
void openOnline();
void openGames();
void openFiles();
void openSystem();
void openSettings();
void openAbout();

Screen *makeSelftest();

}

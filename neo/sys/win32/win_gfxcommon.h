#pragma once

#pragma hdrstop
#include "../../idlib/precompiled.h"

bool GFX_GetWindowDimensions(const glimpParms_t parms, int &x, int &y, int &w, int &h);

void GFX_CreateWindowClasses();

void GFX_CreateWindowClasses();

const char * GetDisplayName(const int deviceNum);

idStr GetDeviceName(const int deviceNum);

bool GetDisplayCoordinates(const int deviceNum, int & x, int & y, int & width, int & height, int & displayHz);

const char * DMDFO(int dmDisplayFixedOutput);

void DumpAllDisplayDevices();


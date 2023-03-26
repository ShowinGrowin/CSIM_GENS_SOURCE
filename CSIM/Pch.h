#pragma once

#define WIN32_LEAN_AND_MEAN

#include <BlueBlur.h>

using namespace std;

// Detours
#include <Windows.h>
#include <detours.h>

// Standard library
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Dependencies
#include <Helpers.h>
#include <INIReader.h>
#include "Resources.h"
#include "AnimationSetPatcher.h"

// Internal headers
#include "ArchiveTreePatcher.h"
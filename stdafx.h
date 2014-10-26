#pragma once

#define WIN32_LEAN_AND_MEAN

#include "targetver.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <assert.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <unordered_map>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <uv.h>
#include "http_parser.h"

#include "persistenthandlewrapper.h"
#include "objectwrap.h"

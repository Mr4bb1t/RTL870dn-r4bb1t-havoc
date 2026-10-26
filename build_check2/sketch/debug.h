#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\debug.h"
#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

#define DEBUG
#define DEBUG_BAUD 115200

#ifdef DEBUG
  #define DEBUG_SER_INIT() Serial.begin(DEBUG_BAUD);
  #define DEBUG_SER_PRINT(...) do { Serial.print(__VA_ARGS__); Serial.flush(); } while(0)
#else
  #define DEBUG_SER_PRINT(...)
  #define DEBUG_SER_INIT()
#endif

#endif

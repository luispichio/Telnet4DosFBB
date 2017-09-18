#ifndef CRC8_h
#define CRC8_h

#include "Arduino.h"
//-----------------------------------------------------------------------------
class CRC8Class {
  public:
    uint8_t calc(uint8_t crc, uint8_t data);
    uint8_t calc(uint8_t crc, uint8_t *frame, uint16_t size);
  private:
};
//------------------------------------------------------------------------------
extern CRC8Class CRC8;
#endif

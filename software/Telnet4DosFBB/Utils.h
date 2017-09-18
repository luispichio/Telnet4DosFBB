#ifndef UTILS_H_
#define UTILS_H_

#include "Arduino.h"

class Utils{
  public:
    Utils();
    static char *GetToken(char *s, char delimiter, uint8_t number, char *token, uint16_t maxTokenLen);
    static int GetIntToken(char *s, char delimiter, uint8_t number);
    static char *ToUpperCase(char *s);
    static uint16_t Min(uint16_t a, uint16_t b);
    static uint16_t Min(uint16_t a, uint16_t b, uint16_t c);
  private:
};

#endif

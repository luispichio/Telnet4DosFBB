/*
MIT License

Copyright (c) 2017 Luis Pichio | https://sites.google.com/site/luispichio/ | https://github.com/luispichio

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "Utils.h"

char *Utils::GetToken(char *s, char delimiter, uint8_t number, char *token, uint16_t maxTokenLen){
  char *_token = token;
  while (*s && number){
    if (*s == delimiter)
      number--;
    s++;
  }
  while (*s && maxTokenLen && (*s != delimiter)){
    *_token = *s;
    s++;
    _token++;
    maxTokenLen--;
  }
  *_token = 0;
  return token;
}

int Utils::GetIntToken(char *s, char delimiter, uint8_t number){
  char token[16];
  Utils::GetToken(s, delimiter, number, token, 15);
  return atoi(token);
}

char *Utils::ToUpperCase(char *s){
  char *p = s;
  while (*p){
    *p = toupper(*p);
    p++;
  }
  return s;
}

uint16_t Utils::Min(uint16_t a, uint16_t b){
  return a < b ? a : b;
}

uint16_t Utils::Min(uint16_t a, uint16_t b, uint16_t c){
  return Utils::Min(min(a, b), c);
}


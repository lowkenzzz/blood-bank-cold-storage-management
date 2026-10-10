#ifndef MFRC522_STUB_H
#define MFRC522_STUB_H
#include "Arduino.h"
class MFRC522 {
public:
  struct Uid { byte uidByte[10]; byte size; } uid;
  MFRC522(int, int) { uid.size = 0; }
  void PCD_Init() {}
  bool PICC_IsNewCardPresent() { return false; }
  bool PICC_ReadCardSerial() { return false; }
  void PICC_HaltA() {}
  void PCD_StopCrypto1() {}
};
#endif

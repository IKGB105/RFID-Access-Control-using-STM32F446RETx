#ifndef MIFARE_H
#define MIFARE_H

#include <stdint.h>
#include "rc522.h"

// ===== Funciones auxiliares MIFARE =====
extern void prepareWriteData(uint8_t levelCode, uint8_t *writeData);
extern void printBlockDataFormatted(uint8_t *blockData);

// ===== Operaciones MIFARE =====
extern int MIFARE_Read(uint8_t blockAddr, uint8_t *recvData);
extern int MIFARE_Write(uint8_t blockAddr, uint8_t *writeData);
extern int MIFARE_Auth(uint8_t authMode, uint8_t blockAddr, uint8_t *key, uint8_t *uid);

#endif

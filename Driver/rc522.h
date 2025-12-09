#ifndef RC522_H
#define RC522_H

#include <stdint.h>

// ================= REGISTROS =================
#define CommandReg      0x01
#define CommIrqReg      0x04
#define ErrorReg        0x06
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define BitFramingReg   0x0D
#define ModeReg         0x11
#define TxControlReg    0x14
#define TxASKReg        0x15
#define Status2Reg      0x08
#define RFCfgReg        0x26
#define CRCResultRegH   0x21
#define CRCResultRegL   0x22
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define VersionReg      0x37
#define CommIEnReg      0x02
#define ControlReg      0x0C

// =============== COMMANDOS PICC ==================
#define PICC_REQA           0x26
#define PICC_ANTICOLL_CL1   0x93
#define PICC_SELECT_CL1     0x93

// =============== COMANDOS PCD ==================
#define PCD_Idle        0x00
#define PCD_Transceive  0x0C
#define PCD_SoftReset   0x0F
#define PCD_AUTHENT     0x0E
#define PCD_MFAuthent   0x0E

// PICC authentication commands
#define PICC_AUTHENT1A  0x60
#define PICC_AUTHENT1B  0x61

// =============== STATUS CODES ==================
#define MI_OK           0x00
#define MI_NOTAGERR     0x01
#define MI_ERR          0x02

// =============== FUNCIONES ======================
void delay_ms(volatile uint32_t ms);
void delay_us(volatile uint32_t us);

void RC522_ResetLow(void);
void RC522_ResetHigh(void);

uint8_t spi_transfer(uint8_t data);
void spi_rw(uint8_t* data, uint8_t count);

void RC522_WriteReg(uint8_t addr, uint8_t val);
uint8_t RC522_ReadReg(uint8_t addr);
void RC522_SetBitMask(uint8_t reg, uint8_t mask);
void RC522_ClearBitMask(uint8_t reg, uint8_t mask);
void RC522_StopCrypto1(void);

// =================== RC522 INIT ==================
void RC522_Init(void);

// Core ToCard function (from reference implementation)
int RC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                 uint8_t *backData, uint16_t *backLen);

int RC522_Transceive(uint8_t* send, uint8_t sendLen, uint8_t* back,
                     uint8_t* backLen, uint8_t validBits);

int RC522_TransceiveEncrypted(uint8_t* send, uint8_t sendLen, uint8_t* back,
                              uint8_t* backLen, uint8_t validBits);

int RC522_RequestA(uint8_t* atqa, uint8_t* atqaLen);
int RC522_AnticollCL1(uint8_t* uid, uint8_t* uidLen);
int RC522_Select(uint8_t* uid);

// CRC calculation
void RC522_CalculateCRC(uint8_t *data, uint8_t len, uint8_t *result);

// Authentication and block operations
int RC522_Auth(uint8_t authMode, uint8_t blockAddr, uint8_t *key, uint8_t *uid);
int RC522_ReadBlock(uint8_t blockAddr, uint8_t *data, uint8_t *uid);
int RC522_WriteBlock(uint8_t blockAddr, uint8_t *data, uint8_t *uid);

#endif

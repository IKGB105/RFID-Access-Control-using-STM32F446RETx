#include <stm32f446xx.h>
#include "mifare.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

// =================== MIFARE HELPER FUNCTIONS ===================

void prepareWriteData(uint8_t levelCode, uint8_t *writeData) {
    memset(writeData, 0, 16);
    if (levelCode == '0') {
        memcpy(writeData, "ADMIN       ", 16);
    } else if (levelCode == '1') {
        memcpy(writeData, "STUDENT     ", 16);
    } else if (levelCode == '2') {
        memcpy(writeData, "VISITOR     ", 16);
    } else {
        memcpy(writeData, "UNKNOWN     ", 16);
    }
}

void printBlockDataFormatted(uint8_t *blockData) {
    USART_SendString("   HEX: ");
    USART_PrintHex(blockData, 16);
    USART_SendString("\r\n   TXT: ");
    
    for(uint8_t i = 0; i < 16; i++) {
        uint8_t ch = blockData[i];
        if (ch >= 32 && ch <= 126) {
            USART_Sendchar(ch);
        } else {
            USART_Sendchar('.');
        }
    }
    USART_SendString("\r\n");
}

// =================== MIFARE OPERATIONS ===================

/**
 * Read block - using RC522_ToCard like the reference
 */
int MIFARE_Read(uint8_t blockAddr, uint8_t *recvData) {
    uint8_t status;
    uint16_t unLen;

    recvData[0] = 0x30;  // READ command
    recvData[1] = blockAddr;
    RC522_CalculateCRC(recvData, 2, &recvData[2]);
    
    status = RC522_ToCard(PCD_Transceive, recvData, 4, recvData, &unLen);

    if ((status != MI_OK) || (unLen != 0x90)) {  // 0x90 = 144 bits = 18 bytes
        return -1;
    }

    return 0;
}

/**
 * Write block - using RC522_ToCard like the reference
 */
int MIFARE_Write(uint8_t blockAddr, uint8_t *writeData) {
    uint8_t status;
    uint16_t recvBits;
    uint8_t i;
    uint8_t buff[18];

    // Step 1: Send write command
    buff[0] = 0xA0;  // WRITE command
    buff[1] = blockAddr;
    RC522_CalculateCRC(buff, 2, &buff[2]);
    
    status = RC522_ToCard(PCD_Transceive, buff, 4, buff, &recvBits);

    if (status != MI_OK) {
        return -1;
    }

    // Step 2: Send data
    if (status == MI_OK) {
        for (i = 0; i < 16; i++) {
            buff[i] = writeData[i];
        }
        RC522_CalculateCRC(buff, 16, &buff[16]);
        status = RC522_ToCard(PCD_Transceive, buff, 18, buff, &recvBits);

        if (status != MI_OK) {
            return -1;
        }
    }

    return 0;
}

/**
 * Authentication - using RC522_ToCard like the reference
 */
int MIFARE_Auth(uint8_t authMode, uint8_t blockAddr, uint8_t *key, uint8_t *uid) {
    uint8_t status;
    uint16_t recvBits;
    uint8_t i;
    uint8_t buff[12];

    // Build auth command
    buff[0] = authMode;
    buff[1] = blockAddr;
    for (i = 0; i < 6; i++) {
        buff[i + 2] = key[i];
    }
    for (i = 0; i < 4; i++) {
        buff[i + 8] = uid[i];
    }

    status = RC522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);

    if ((status != MI_OK) || (!(RC522_ReadReg(Status2Reg) & 0x08))) {
        return -1;
    }

    return 0;
}

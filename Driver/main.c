#include <stm32f446xx.h>
#include <stdio.h>
#include <string.h>

#include "conf.h"
#include "usart.h"
#include "mifare.h"
#include "rc522.h"

// =================== SYSTEM CONFIG ===================

void SystemClock_Config(void) {
    // Enable HSE
    RCC->CR |= RCC_CR_HSEON;
    while(!(RCC->CR & RCC_CR_HSERDY));
    
    // Configure PLL
    RCC->PLLCFGR = 0;
    RCC->PLLCFGR |= (8 << RCC_PLLCFGR_PLLM_Pos);      // PLLM = 8
    RCC->PLLCFGR |= (360 << RCC_PLLCFGR_PLLN_Pos);    // PLLN = 360
    RCC->PLLCFGR |= (0 << RCC_PLLCFGR_PLLP_Pos);      // PLLP = 2
    RCC->PLLCFGR |= (7 << RCC_PLLCFGR_PLLQ_Pos);      // PLLQ = 8
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;
    
    // Enable PLL
    RCC->CR |= RCC_CR_PLLON;
    while(!(RCC->CR & RCC_CR_PLLRDY));
    
    // Configure Flash latency
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | 
                 FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_5WS;
    
    // Configure APB dividers
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;    // AHB = 180MHz
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;   // APB1 = 45MHz
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;   // APB2 = 90MHz
    
    // Switch to PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
    
    // Enable GPIO and peripheral clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    
    volatile uint32_t temp = RCC->AHB1ENR;
    (void)temp;
}

// =================== MAIN FUNCTION ===================

int main(void) {
    char msg[128];
    
    // ===== Initialize System =====
    SystemClock_Config();
    confGPIO();
    confUSART();
    confSPI();
    
    // ===== Debug Output =====
    USART_SendString("\r\n\r\n");
    USART_SendString("=========================================\r\n");
    USART_SendString("  STM32F446 RFID + NodeMCU System\r\n");
    USART_SendString("=========================================\r\n");
    USART_SendString("USART2: Debug (PuTTY)\r\n");
    USART_SendString("USART1: NodeMCU Communication\r\n");
    delay_ms(100);
    
    // Send init message to NodeMCU
    USART1_SendString("STM32_READY\r\n");
    
    // ===== Initialize RC522 =====
    RC522_ResetLow();
    delay_ms(50);
    RC522_ResetHigh();
    delay_ms(100);
    
    uint8_t version = RC522_ReadReg(VersionReg);
    sprintf(msg, "RC522 Version: 0x%02X\r\n", version);
    USART_SendString(msg);
    
    RC522_Init();
    RC522_SetBitMask(TxControlReg, 0x03);
    
    USART_SendString("RC522 Initialized\r\n");
    USART_SendString("=========================================\r\n");
    USART_SendString("Place a card...\r\n\r\n");
    
    // ===== MIFARE Setup =====
    uint8_t atqa[2], atqaLen, uid[10], uidLen;
    uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t cardCount = 0;
    uint8_t pendingWrite = 0;
    
    // ===== Main Loop =====
    while(1) {
        // Check for NodeMCU commands (0, 1, or 2)
        uint8_t levelCode = 0;
        uint32_t uart_wait_count = 0;
        
        while(USART1_Available() && uart_wait_count < 100) {
            levelCode = USART1_Receivechar();
            
            // Debug: show received character
            sprintf(msg, "[RX: 0x%02X='%c'] ", levelCode, 
                   (levelCode >= 32 && levelCode <= 126) ? levelCode : '.');
            USART_SendString(msg);
            
            if (levelCode == '0' || levelCode == '1' || levelCode == '2') {
                char level_name[20];
                if (levelCode == '0') strcpy(level_name, "ADMIN");
                else if (levelCode == '1') strcpy(level_name, "STUDENT");
                else strcpy(level_name, "VISITOR");
                
                sprintf(msg, "\r\n[NodeMCU] Write level: %s\r\n", level_name);
                USART_SendString(msg);
                pendingWrite = levelCode;
                break;
            }
            uart_wait_count++;
        }
        
        // Detect card
        if(RC522_RequestA(atqa, &atqaLen) == 0) {
            if(RC522_AnticollCL1(uid, &uidLen) == 0) {
                if(RC522_Select(uid) == 0) {
                    cardCount++;
                    
                    // Send UID to NodeMCU
                    sprintf(msg, "\r\n[%u] CARD DETECTED\r\n", (unsigned int)cardCount);
                    USART_SendString(msg);
                    
                    USART_SendString("UID: ");
                    USART_PrintHex(uid, 4);
                    USART_SendString("\r\n");
                    
                    sprintf(msg, "UID:%02X%02X%02X%02X\r\n", uid[0], uid[1], uid[2], uid[3]);
                    USART1_SendString(msg);
                    
                    // ===== READ Block 4 =====
                    USART_SendString("\n1. READING block 4...\r\n");
                    uint8_t blockData[18];
                    
                    if(MIFARE_Auth(PICC_AUTHENT1A, 4, keyA, uid) == 0) {
                        if(MIFARE_Read(4, blockData) == 0) {
                            printBlockDataFormatted(blockData);
                            
                            // Send block data to NodeMCU
                            USART1_SendString("DATA:");
                            for(uint8_t i = 0; i < 16; i++) {
                                sprintf(msg, "%02X", blockData[i]);
                                USART1_SendString(msg);
                            }
                            USART1_SendString("\r\n");
                        } else {
                            USART_SendString("   FAILED\r\n");
                            USART1_SendString("READ:FAIL\r\n");
                        }
                    } else {
                        USART_SendString("   Auth FAILED\r\n");
                        USART1_SendString("AUTH:FAIL\r\n");
                    }
                    
                    delay_ms(50);
                    
                    // ===== WRITE Block 4 (if pending) =====
                    if (pendingWrite >= '0' && pendingWrite <= '2') {
                        USART_SendString("\n2. WRITING to block 4...\r\n");
                        uint8_t writeData[16];
                        prepareWriteData(pendingWrite, writeData);
                        
                        USART_SendString("   Data to write: ");
                        printBlockDataFormatted(writeData);
                        
                        if(MIFARE_Auth(PICC_AUTHENT1A, 4, keyA, uid) == 0) {
                            if(MIFARE_Write(4, writeData) == 0) {
                                USART_SendString("   WRITE OK\r\n");
                                USART1_SendString("WRITE:OK\r\n");
                            } else {
                                USART_SendString("   WRITE FAILED\r\n");
                                USART1_SendString("WRITE:FAIL\r\n");
                            }
                        } else {
                            USART_SendString("   Auth FAILED\r\n");
                            USART1_SendString("AUTH:FAIL\r\n");
                        }
                        
                        pendingWrite = 0;
                    }
                    
                    // Stop crypto session
                    RC522_StopCrypto1();
                    USART_SendString("\r\n=== DONE ===\r\n");
                    delay_ms(3000);
                }
            }
        }
        
        delay_ms(300);
    }
    
    return 0;
}

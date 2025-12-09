/**
 * @file main.c
 * @brief Lector RFID STM32F446 con integración NodeMCU
 * 
 * Función main limpia utilizando librerías de controladores modulares:
 * - conf.c/h: Configuración del sistema (GPIO, UART, SPI, Reloj)
 * - usart.c/h: Funciones de comunicación UART
 * - mifare.c/h: Operaciones MIFARE y funciones auxiliares
 * - rc522.c/h: Interfaz de hardware RC522
 */

#include <stm32f446xx.h>
#include <stdio.h>
#include <string.h>

#include "conf.h"
#include "usart.h"
#include "mifare.h"
#include "rc522.h"

// =================== CONFIGURACIÓN DEL SISTEMA ===================

void SystemClock_Config(void) {
    // Habilitar HSE
    RCC->CR |= RCC_CR_HSEON;
    while(!(RCC->CR & RCC_CR_HSERDY));
    
    // Configurar PLL
    RCC->PLLCFGR = 0;
    RCC->PLLCFGR |= (8 << RCC_PLLCFGR_PLLM_Pos);      // PLLM = 8
    RCC->PLLCFGR |= (360 << RCC_PLLCFGR_PLLN_Pos);    // PLLN = 360
    RCC->PLLCFGR |= (0 << RCC_PLLCFGR_PLLP_Pos);      // PLLP = 2
    RCC->PLLCFGR |= (7 << RCC_PLLCFGR_PLLQ_Pos);      // PLLQ = 8
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;
    
    // Habilitar PLL
    RCC->CR |= RCC_CR_PLLON;
    while(!(RCC->CR & RCC_CR_PLLRDY));
    
    // Configurar latencia Flash
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | 
                 FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_5WS;
    
    // Configurar divisores APB
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;    // AHB = 180MHz
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;   // APB1 = 45MHz
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;   // APB2 = 90MHz
    
    // Cambiar a PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
    
    // Habilitar relojes de GPIO y periféricos
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    
    volatile uint32_t temp = RCC->AHB1ENR;
    (void)temp;
}

// =================== FUNCIÓN MAIN ===================

int main(void) {
    char msg[128];
    
    // ===== Inicializar sistema =====
    SystemClock_Config();
    confGPIO();
    confUSART();
    confSPI();
    
    // ===== Salida de debug =====
    USART_SendString("\r\n\r\n");
    USART_SendString("=========================================\r\n");
    USART_SendString("  STM32F446 RFID + Sistema NodeMCU\r\n");
    USART_SendString("=========================================\r\n");
    USART_SendString("USART2: Debug (PuTTY)\r\n");
    USART_SendString("USART1: Comunicación NodeMCU\r\n");
    delay_ms(100);
    
    // Enviar mensaje de inicio a NodeMCU
    USART1_SendString("STM32_READY\r\n");
    
    // ===== Inicializar RC522 =====
    RC522_ResetLow();
    delay_ms(50);
    RC522_ResetHigh();
    delay_ms(100);
    
    uint8_t version = RC522_ReadReg(VersionReg);
    sprintf(msg, "Versión RC522: 0x%02X\r\n", version);
    USART_SendString(msg);
    
    RC522_Init();
    RC522_SetBitMask(TxControlReg, 0x03);
    
    USART_SendString("RC522 Inicializado\r\n");
    USART_SendString("=========================================\r\n");
    USART_SendString("Acerca una tarjeta...\r\n\r\n");
    
    // ===== Configuración MIFARE =====
    uint8_t atqa[2], atqaLen, uid[10], uidLen;
    uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t cardCount = 0;
    uint8_t pendingWrite = 0;
    
    // ===== Bucle principal =====
    while(1) {
        // Verificar comandos de NodeMCU (0, 1, o 2)
        uint8_t levelCode = 0;
        uint32_t uart_wait_count = 0;
        
        while(USART1_Available() && uart_wait_count < 100) {
            levelCode = USART1_Receivechar();
            
            // Debug: mostrar carácter recibido
            sprintf(msg, "[RX: 0x%02X='%c'] ", levelCode, 
                   (levelCode >= 32 && levelCode <= 126) ? levelCode : '.');
            USART_SendString(msg);
            
            if (levelCode == '0' || levelCode == '1' || levelCode == '2') {
                char level_name[20];
                if (levelCode == '0') strcpy(level_name, "ADMIN");
                else if (levelCode == '1') strcpy(level_name, "ESTUDIANTE");
                else strcpy(level_name, "VISITANTE");
                
                sprintf(msg, "\r\n[NodeMCU] Nivel de escritura: %s\r\n", level_name);
                USART_SendString(msg);
                pendingWrite = levelCode;
                break;
            }
            uart_wait_count++;
        }
        
        // Detectar tarjeta
        if(RC522_RequestA(atqa, &atqaLen) == 0) {
            if(RC522_AnticollCL1(uid, &uidLen) == 0) {
                if(RC522_Select(uid) == 0) {
                    cardCount++;
                    
                    // Enviar UID a NodeMCU
                    sprintf(msg, "\r\n[%u] TARJETA DETECTADA\r\n", (unsigned int)cardCount);
                    USART_SendString(msg);
                    
                    USART_SendString("UID: ");
                    USART_PrintHex(uid, 4);
                    USART_SendString("\r\n");
                    
                    sprintf(msg, "UID:%02X%02X%02X%02X\r\n", uid[0], uid[1], uid[2], uid[3]);
                    USART1_SendString(msg);
                    
                    // ===== LEER Bloque 4 =====
                    USART_SendString("\n1. LEYENDO bloque 4...\r\n");
                    uint8_t blockData[18];
                    
                    if(MIFARE_Auth(PICC_AUTHENT1A, 4, keyA, uid) == 0) {
                        if(MIFARE_Read(4, blockData) == 0) {
                            printBlockDataFormatted(blockData);
                            
                            // Enviar datos del bloque a NodeMCU
                            USART1_SendString("DATA:");
                            for(uint8_t i = 0; i < 16; i++) {
                                sprintf(msg, "%02X", blockData[i]);
                                USART1_SendString(msg);
                            }
                            USART1_SendString("\r\n");
                        } else {
                            USART_SendString("   FALLÓ\r\n");
                            USART1_SendString("READ:FAIL\r\n");
                        }
                    } else {
                        USART_SendString("   Autenticación FALLÓ\r\n");
                        USART1_SendString("AUTH:FAIL\r\n");
                    }
                    
                    delay_ms(50);
                    
                    // ===== ESCRIBIR Bloque 4 (si está pendiente) =====
                    if (pendingWrite >= '0' && pendingWrite <= '2') {
                        USART_SendString("\n2. ESCRIBIENDO en bloque 4...\r\n");
                        uint8_t writeData[16];
                        prepareWriteData(pendingWrite, writeData);
                        
                        USART_SendString("   Datos a escribir: ");
                        printBlockDataFormatted(writeData);
                        
                        if(MIFARE_Auth(PICC_AUTHENT1A, 4, keyA, uid) == 0) {
                            if(MIFARE_Write(4, writeData) == 0) {
                                USART_SendString("   ESCRITURA OK\r\n");
                                USART1_SendString("WRITE:OK\r\n");
                            } else {
                                USART_SendString("   ESCRITURA FALLÓ\r\n");
                                USART1_SendString("WRITE:FAIL\r\n");
                            }
                        } else {
                            USART_SendString("   Autenticación FALLÓ\r\n");
                            USART1_SendString("AUTH:FAIL\r\n");
                        }
                        
                        pendingWrite = 0;
                    }
                    
                    // Detener sesión criptográfica
                    RC522_StopCrypto1();
                    USART_SendString("\r\n=== COMPLETADO ===\r\n");
                    delay_ms(3000);
                }
            }
        }
        
        delay_ms(300);
    }
    
    return 0;
}

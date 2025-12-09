#include <stm32f446xx.h>
#include "usart.h"
#include <stdio.h>

// ========== USART2 Functions (Debug/PuTTY) ==========

void USART2_Sendchar(uint8_t ch) {
    while(!(USART2->SR & USART_SR_TXE));
    USART2->DR = ch;
    while(!(USART2->SR & USART_SR_TC));
}

uint8_t USART2_Receivechar(void) {
    while(!(USART2->SR & USART_SR_RXNE));
    return (uint8_t)USART2->DR;
}

void USART2_SendString(const char *str) {
    while(*str) {
        USART2_Sendchar(*str++);
    }
}

void USART2_NewLine(const char *str) {
    USART2_SendString(str);
    USART2_SendString("\r\n");
}

uint8_t USART2_Available(void) {
    return (USART2->SR & USART_SR_RXNE) ? 1 : 0;
}

void USART2_PrintHex(uint8_t *buffer, uint8_t len) {
    char msg[128];
    for(uint8_t i = 0; i < len; i++) {
        sprintf(msg, "%02X ", buffer[i]);
        USART2_SendString(msg);
    }
}

// ========== USART1 Functions (NodeMCU) ==========

void USART1_Sendchar(uint8_t ch) {
    while(!(USART1->SR & USART_SR_TXE));
    USART1->DR = ch;
    while(!(USART1->SR & USART_SR_TC));
}

uint8_t USART1_Receivechar(void) {
    while(!(USART1->SR & USART_SR_RXNE));
    return (uint8_t)USART1->DR;
}

void USART1_SendString(const char *str) {
    while(*str) {
        USART1_Sendchar(*str++);
    }
}

void USART1_NewLine(const char *str) {
    USART1_SendString(str);
    USART1_SendString("\r\n");
}

uint8_t USART1_Available(void) {
    return (USART1->SR & USART_SR_RXNE) ? 1 : 0;
}

// ========== Legacy Functions (USART2 compatibility) ==========

void USART_Sendchar(uint8_t ch) {
    USART2_Sendchar(ch);
}

uint8_t USART_Receivechar(void) {
    return USART2_Receivechar();
}

void USART_SendString(const char *str) {
    USART2_SendString(str);
}

void USART_NewLine(const char *str) {
    USART2_NewLine(str);
}

void USART_PrintHex(uint8_t *buffer, uint8_t len) {
    USART2_PrintHex(buffer, len);
}

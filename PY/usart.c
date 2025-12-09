#include <stm32f446xx.h>
#include "usart.h"
#include <stdio.h>

// ========== Funciones USART2 (Debug/PuTTY) ==========

void USART_Sendchar(uint8_t ch) {
    while(!(USART2->SR & USART_SR_TXE));
    USART2->DR = ch;
    while(!(USART2->SR & USART_SR_TC));
}

uint8_t USART_Receivechar(void) {
    while(!(USART2->SR & USART_SR_RXNE));
    return (uint8_t)USART2->DR;
}

void USART_SendString(const char *str) {
    while(*str) {
        USART_Sendchar(*str++);
    }
}

void USART_PrintHex(uint8_t *buffer, uint8_t len) {
    char msg[128];
    for(uint8_t i = 0; i < len; i++) {
        sprintf(msg, "%02X ", buffer[i]);
        USART_SendString(msg);
    }
}

// ========== Funciones USART1 (NodeMCU) ==========

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

uint8_t USART1_Available(void) {
    return (USART1->SR & USART_SR_RXNE) ? 1 : 0;
}

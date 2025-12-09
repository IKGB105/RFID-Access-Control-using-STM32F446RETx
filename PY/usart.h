#ifndef USART_H
#define USART_H

#include <stdint.h>

// ===== Funciones USART2 (Debug/PuTTY) =====
extern void USART_Sendchar(uint8_t ch);
extern uint8_t USART_Receivechar(void);
extern void USART_SendString(const char *str);
extern void USART_PrintHex(uint8_t *buffer, uint8_t len);

// ===== Funciones USART1 (NodeMCU) =====
extern void USART1_Sendchar(uint8_t ch);
extern uint8_t USART1_Receivechar(void);
extern void USART1_SendString(const char *str);
extern uint8_t USART1_Available(void);

#endif

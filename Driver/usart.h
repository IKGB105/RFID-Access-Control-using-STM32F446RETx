#ifndef USART_H
#define USART_H

#include <stdint.h>

// ===== USART2 Functions (Debug/PuTTY) =====
extern void USART2_Sendchar(uint8_t ch);
extern uint8_t USART2_Receivechar(void);
extern void USART2_SendString(const char *str);
extern void USART2_NewLine(const char *str);
extern uint8_t USART2_Available(void);
extern void USART2_PrintHex(uint8_t *buffer, uint8_t len);

// ===== USART1 Functions (NodeMCU) =====
extern void USART1_Sendchar(uint8_t ch);
extern uint8_t USART1_Receivechar(void);
extern void USART1_SendString(const char *str);
extern void USART1_NewLine(const char *str);
extern uint8_t USART1_Available(void);

// ===== Legacy Functions (USART2 compatibility) =====
extern void USART_Sendchar(uint8_t ch);
extern uint8_t USART_Receivechar(void);
extern void USART_SendString(const char *str);
extern void USART_NewLine(const char *str);
extern void USART_PrintHex(uint8_t *buffer, uint8_t len);

#endif

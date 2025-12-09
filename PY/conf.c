#include "conf.h"
#include <stm32f446xx.h>

// USART2: PA2(TX), PA3(RX) - PuTTY/USB
// USART1: PA9(TX), PA10(RX) - NodeMCU
// PA4 : NSS/CS
// PA5 : SCLK
// PA6 : MISO
// PA7 : MOSI
// PA8 : Reset RC522

void confRCC(void) {
    RCC->AHB1ENR |= (1 << 0) | (1 << 2); // GPIOA y GPIOC
    
    // Reloj SPI1
    RCC->APB2ENR |= (1 << 12);
    
    // Reloj USART2 (PuTTY)
    RCC->APB1ENR |= (1 << 17);
    
    // Reloj USART1 (NodeMCU)
    RCC->APB2ENR |= (1 << 4);
}

void confGPIO(void) {
    // ===== PA4 = NSS/CS (Salida GPIO control manual) =====
    GPIOA->MODER &= ~(3 << (2*4));      // Limpiar bits de modo
    GPIOA->MODER |= (1 << (2*4));       // Modo salida
    GPIOA->OTYPER &= ~(1 << 4);         // Push-pull
    GPIOA->OSPEEDR |= (3 << (2*4));     // Velocidad alta
    GPIOA->PUPDR &= ~(3 << (2*4));      // Sin pull
    GPIOA->BSRR = (1 << 4);             // CS ALTO (inactivo)
    
    // ===== PA5=SCK, PA6=MISO, PA7=MOSI (AF5 - SPI1) =====
    GPIOA->MODER &= ~((3 << (2*5)) | (3 << (2*6)) | (3 << (2*7)));
    GPIOA->MODER |= (2<<(2*5)) | (2<<(2*6)) | (2<<(2*7));  // Modo AF
    GPIOA->AFR[0] |= (5 <<(4*5)) | (5<<(4*6)) | (5<<(4*7)); // AF5
    GPIOA->OSPEEDR |= (3 << (2*5)) | (3 << (2*6)) | (3 << (2*7)); // Velocidad alta
    
    // ===== PA8 como pin Reset RC522 (Salida) =====
    GPIOA->MODER &= ~(3 << (2*8));      // Limpiar bits de modo
    GPIOA->MODER |= (1 << (2*8));       // Modo salida
    GPIOA->OSPEEDR |= (3 << (2*8));     // Velocidad alta
    GPIOA->OTYPER &= ~(1 << 8);         // Push-pull
    GPIOA->PUPDR &= ~(3 << (2*8));      // Sin pull-up/pull-down
    GPIOA->BSRR = (1 << 8);             // Establecer alto
    
    // LED en PC13
    GPIOC->MODER &= ~(3 << (2*13));
    GPIOC->MODER |= (1<<(2*13));
    GPIOC->OSPEEDR |= (3<<(2*13));
    GPIOC->ODR |= (1<<13);
    
    // ===== USART2 (PuTTY) - PA2(TX), PA3(RX) =====
    GPIOA->MODER &= ~((3 << (2*2)) | (3 << (2*3)));
    GPIOA->MODER |= (2 << (2*2)) | (2 <<(2*3)); // Función alterna
    GPIOA->AFR[0] |= (7<<(4*2)) | (7<<(4*3));   // AF7
    
    // ===== USART1 (NodeMCU) - PA9(TX), PA10(RX) =====
    GPIOA->MODER &= ~((3 << (2*9)) | (3 << (2*10)));
    GPIOA->MODER |= (2 << (2*9)) | (2 <<(2*10)); // Función alterna
    GPIOA->AFR[1] |= (7<<(4*1)) | (7<<(4*2));    // AF7 (AFR[1] porque PA9-PA10)
}

void confSPI(void) {
    // Configuración SPI1 para RC522
    // Reloj APB2: 90MHz, necesita ~2-4MHz para RC522
    
    // 1. Reset SPI1
    SPI1->CR1 = 0;
    
    // 2. Configurar como Master
    SPI1->CR1 |= SPI_CR1_MSTR;
    
    // 3. Modo SPI 0 (CPOL=0, CPHA=0) - Requerido para RC522
    SPI1->CR1 &= ~SPI_CR1_CPOL;  // Reloj reposo BAJO
    SPI1->CR1 &= ~SPI_CR1_CPHA;  // Datos en primer flanco
    
    // 4. Velocidad: APB2/32 = 90MHz/32 = 2.8MHz
    SPI1->CR1 &= ~SPI_CR1_BR;    // Limpiar bits BR
    SPI1->CR1 |= (4 << 3);       // BR[2:0] = 100 = /32
    
    // 5. NSS software (CS control manual)
    SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;
    
    // 6. MSB primero
    SPI1->CR1 &= ~SPI_CR1_LSBFIRST;
    
    // 7. Modo full duplex
    SPI1->CR1 &= ~SPI_CR1_RXONLY;
    
    // 8. Datos de 8 bits
    SPI1->CR1 &= ~SPI_CR1_DFF;
    
    // 9. Habilitar SPI
    SPI1->CR1 |= SPI_CR1_SPE;
}

void confUSART(void) {
    // Reloj del sistema: 180MHz
    // Reloj APB1 (USART2): 180MHz / 4 = 45MHz
    // Reloj APB2 (USART1): 180MHz / 2 = 90MHz
    
    // ===== USART2 (PuTTY) - APB1 @ 45MHz =====
    USART2->CR1 = 0;  // Limpiar primero
    USART2->CR1 |= (1<<3)     // HABILITAR TX
                |(1<<2)     // HABILITAR RX
                |(1<<13);   // HABILITAR USART
    // BRR = 45,000,000 / 9600 = 4687.5 ≈ 4688 = 0x1250
    USART2->BRR = 0x1250;    // 9600 baud @ 45MHz APB1
    
    // ===== USART1 (NodeMCU) - APB2 @ 90MHz =====
    USART1->CR1 = 0;  // Limpiar primero
    USART1->CR1 |= (1<<3)     // HABILITAR TX
                |(1<<2)     // HABILITAR RX
                |(1<<13);   // HABILITAR USART
    // BRR = 90,000,000 / 9600 = 9375 = 0x249F
    USART1->BRR = 0x249F;    // 9600 baud @ 90MHz APB2
}

void confALL(void) {
    confRCC();
    confGPIO();
    confUSART();
    confSPI();
}

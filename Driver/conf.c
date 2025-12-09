#include "conf.h"
#include <stm32f446xx.h>

// USART2: PA2(TX), PA3(RX) - PuTTY/USB
// USART1: PA9(TX), PA10(RX) - NodeMCU
// PA4 : NSS/CS
// PA5 : SCLK
// PA6 : MISO
// PA7 : MOSI
// PA8 : RC522 Reset

void confRCC(void) {
    RCC->AHB1ENR |= (1 << 0) | (1 << 2); // GPIOA and GPIOC
    
    // SPI1 Clock
    RCC->APB2ENR |= (1 << 12);
    
    // USART2 Clock (PuTTY)
    RCC->APB1ENR |= (1 << 17);
    
    // USART1 Clock (NodeMCU)
    RCC->APB2ENR |= (1 << 4);
}

void confGPIO(void) {
    // ===== PA4 = NSS/CS (GPIO Output for manual control) =====
    GPIOA->MODER &= ~(3 << (2*4));      // Clear mode bits
    GPIOA->MODER |= (1 << (2*4));       // Output mode
    GPIOA->OTYPER &= ~(1 << 4);         // Push-pull
    GPIOA->OSPEEDR |= (3 << (2*4));     // High speed
    GPIOA->PUPDR &= ~(3 << (2*4));      // No pull
    GPIOA->BSRR = (1 << 4);             // CS HIGH initially (inactive)
    
    // ===== PA5=SCK, PA6=MISO, PA7=MOSI (AF5 - SPI1) =====
    GPIOA->MODER &= ~((3 << (2*5)) | (3 << (2*6)) | (3 << (2*7)));
    GPIOA->MODER |= (2<<(2*5)) | (2<<(2*6)) | (2<<(2*7));  // AF mode
    GPIOA->AFR[0] |= (5 <<(4*5)) | (5<<(4*6)) | (5<<(4*7)); // AF5
    GPIOA->OSPEEDR |= (3 << (2*5)) | (3 << (2*6)) | (3 << (2*7)); // High speed
    
    // ===== PA8 as RC522 Reset Pin (Output) =====
    GPIOA->MODER &= ~(3 << (2*8));      // Clear mode bits
    GPIOA->MODER |= (1 << (2*8));       // Output mode
    GPIOA->OSPEEDR |= (3 << (2*8));     // High speed
    GPIOA->OTYPER &= ~(1 << 8);         // Push-pull
    GPIOA->PUPDR &= ~(3 << (2*8));      // No pull-up/pull-down
    GPIOA->BSRR = (1 << 8);             // Set high initially
    
    // LED on PC13
    GPIOC->MODER &= ~(3 << (2*13));
    GPIOC->MODER |= (1<<(2*13));
    GPIOC->OSPEEDR |= (3<<(2*13));
    GPIOC->ODR |= (1<<13);
    
    // ===== USART2 (PuTTY) - PA2(TX), PA3(RX) =====
    GPIOA->MODER &= ~((3 << (2*2)) | (3 << (2*3)));
    GPIOA->MODER |= (2 << (2*2)) | (2 <<(2*3)); // ALTERNATE FUNCTION
    GPIOA->AFR[0] |= (7<<(4*2)) | (7<<(4*3));   // AF7
    
    // ===== USART1 (NodeMCU) - PA9(TX), PA10(RX) =====
    GPIOA->MODER &= ~((3 << (2*9)) | (3 << (2*10)));
    GPIOA->MODER |= (2 << (2*9)) | (2 <<(2*10)); // ALTERNATE FUNCTION
    GPIOA->AFR[1] |= (7<<(4*1)) | (7<<(4*2));    // AF7 (AFR[1] because PA9-PA10)
}

void confSPI(void) {
    // SPI1 Configuration for RC522
    // APB2 Clock: 90MHz, need ~2-4MHz for RC522
    
    // 1. Reset SPI1
    SPI1->CR1 = 0;
    
    // 2. Configure as Master
    SPI1->CR1 |= SPI_CR1_MSTR;
    
    // 3. SPI Mode 0 (CPOL=0, CPHA=0) - Required for RC522
    SPI1->CR1 &= ~SPI_CR1_CPOL;  // Clock idle LOW
    SPI1->CR1 &= ~SPI_CR1_CPHA;  // Data sampled on first edge
    
    // 4. Baud Rate: APB2/32 = 90MHz/32 = 2.8MHz
    SPI1->CR1 &= ~SPI_CR1_BR;    // Clear BR bits
    SPI1->CR1 |= (4 << 3);       // BR[2:0] = 100 = /32
    
    // 5. Software NSS (CS controlled manually)
    SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;
    
    // 6. MSB first
    SPI1->CR1 &= ~SPI_CR1_LSBFIRST;
    
    // 7. Full duplex mode
    SPI1->CR1 &= ~SPI_CR1_RXONLY;
    
    // 8. 8-bit data frame
    SPI1->CR1 &= ~SPI_CR1_DFF;
    
    // 9. Enable SPI
    SPI1->CR1 |= SPI_CR1_SPE;
}

void confUSART(void) {
    // System Clock: 180MHz
    // APB1 Clock (USART2): 180MHz / 4 = 45MHz
    // APB2 Clock (USART1): 180MHz / 2 = 90MHz
    
    // ===== USART2 (PuTTY) - APB1 @ 45MHz =====
    USART2->CR1 = 0;  // Clear first
    USART2->CR1 |= (1<<3)     // ENABLE TX
                |(1<<2)     // ENABLE RX
                |(1<<13);   // ENABLE USART
    // BRR = 45,000,000 / 9600 = 4687.5 ≈ 4688 = 0x1250
    USART2->BRR = 0x1250;    // 9600 baud @ 45MHz APB1
    
    // ===== USART1 (NodeMCU) - APB2 @ 90MHz =====
    USART1->CR1 = 0;  // Clear first
    USART1->CR1 |= (1<<3)     // ENABLE TX
                |(1<<2)     // ENABLE RX
                |(1<<13);   // ENABLE USART
    // BRR = 90,000,000 / 9600 = 9375 = 0x249F
    USART1->BRR = 0x249F;    // 9600 baud @ 90MHz APB2
}

void confALL(){
	confRCC();
	confGPIO();
	confUSART();
	confSPI();
}
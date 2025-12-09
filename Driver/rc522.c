#include "rc522.h"
#include "comusart.h"
#include "stm32f446xx.h"
#include <stdio.h>

// =================== DELAYS ======================
void delay_ms(volatile uint32_t ms) {
    // Corregido para 180MHz: ~45000 ciclos por ms
    while(ms--) {
        for (volatile int i = 0; i < 45000; i++);
    }
}

void delay_us(volatile uint32_t us) {
    // Para 180MHz: ~180 ciclos por microsegundo
    us = us * 45;
    while(us--) {
        __NOP();
    }
}

// =================== UART ========================
// NOTA: USART_Sendchar y USART_SendString están definidas en main.c

// =================== GPIO CONTROL ================
void RC522_ResetLow(void) { 
    GPIOA->BSRR = (1 << (8 + 16));  // PA8 LOW
}

void RC522_ResetHigh(void) { 
    GPIOA->BSRR = (1 << 8);         // PA8 HIGH
}

// =================== SPI COMMUNICATION ===========
uint8_t spi_transfer(uint8_t data) {
    // Esperar buffer TX vac�o
    while(!(SPI1->SR & SPI_SR_TXE));
    
    // Enviar dato
    SPI1->DR = data;
    
    // Esperar dato recibido
    while(!(SPI1->SR & SPI_SR_RXNE));
    
    // Leer dato
    return SPI1->DR;
}

void spi_rw(uint8_t *data, uint8_t count) {
    // CS LOW
    GPIOA->BSRR = (1 << (4 + 16));
    delay_us(2);
    
    // Transferir bytes
    for(uint8_t i = 0; i < count; i++) {
        data[i] = spi_transfer(data[i]);
    }
    
    // Esperar fin de transmisi�n
    while(SPI1->SR & SPI_SR_BSY);
    delay_us(2);
    
    // CS HIGH
    GPIOA->BSRR = (1 << 4);
}

// =================== RC522 REGISTERS =============
void RC522_WriteReg(uint8_t addr, uint8_t val) {
    uint8_t frame[2];
    
    // Direcci�n de escritura: (addr << 1) & 0x7E
    // Bit 0 = 0 (write), Bit 7 = 0 (no address increment)
    frame[0] = (addr << 1) & 0x7E;
    frame[1] = val;
    
    spi_rw(frame, 2);
}

uint8_t RC522_ReadReg(uint8_t addr) {
    uint8_t frame[2];
    
    // Direcci�n de lectura: ((addr << 1) & 0x7E) | 0x80
    // Bit 0 = 1 (read), Bit 7 = 0 (no address increment)
    frame[0] = ((addr << 1) & 0x7E) | 0x80;
    frame[1] = 0x00;  // El dato viene en este byte
    
    spi_rw(frame, 2);
    
    // El dato v�lido viene en el SEGUNDO byte
    return frame[1];
}

void RC522_SetBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) | mask);
}

void RC522_ClearBitMask(uint8_t reg, uint8_t mask) {
    RC522_WriteReg(reg, RC522_ReadReg(reg) & (~mask));
}

// =================== RC522 INIT ==================
void RC522_Init(void) {
    // 1. Soft Reset
    RC522_WriteReg(CommandReg, PCD_SoftReset);
    delay_ms(50);
    
    // 2. Clear FIFO e interrupciones
    RC522_WriteReg(FIFOLevelReg, 0x80);
    RC522_WriteReg(CommIrqReg, 0x7F);
    
    // 3. Timer configuration
    RC522_WriteReg(TModeReg, 0x8D);        // TAuto=1, timer auto-reload
    RC522_WriteReg(TPrescalerReg, 0x3E);   
    RC522_WriteReg(TReloadRegH, 0x00);     
    RC522_WriteReg(TReloadRegL, 30);       
    
    // 4. Modulaci�n ASK 100%
    RC522_WriteReg(TxASKReg, 0x40);
    
    // 5. Modo config
    RC522_WriteReg(ModeReg, 0x3D);
    
    // 6. Configurar RxGain - M�XIMA SENSIBILIDAD
    RC522_WriteReg(RFCfgReg, 0x7F);
    
    // 7. Inicializar CommandReg
    RC522_WriteReg(CommandReg, PCD_Idle);
    
    delay_ms(10);
}

// =================== RC522_ToCard (CORE TRANSCEIVE) =================
/**
 * This is the CRITICAL function - mirrors STM32F103 reference implementation
 * Handles ALL transceive operations with proper command-specific IRQ setup
 */
int RC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                 uint8_t *backData, uint16_t *backLen) {
    
    uint8_t status = MI_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint16_t i;

    // Step 1: Set up interrupt based on command type
    switch (command) {
        case PCD_AUTHENT:       // Authentication
            irqEn = 0x12;       // Enable bits 1 and 4
            waitIRq = 0x10;     // Wait for IdleIRq (bit 4) only
            break;
        case PCD_Transceive:    // Transmit/Receive
            irqEn = 0x77;       // Enable bits 0,1,2,4,5,6
            waitIRq = 0x30;     // Wait for RxIRq (bit 5) or IdleIRq (bit 4)
            break;
        default:
            return status;
    }

    // Step 2: Setup interrupts
    RC522_WriteReg(CommIEnReg, irqEn | 0x80);    // Enable interrupts + Enable IRQ push-pull
    RC522_WriteReg(CommIrqReg, 0x7F);             // Clear all interrupt flags
    RC522_SetBitMask(FIFOLevelReg, 0x80);         // Flush FIFO

    // Step 3: Go to IDLE state
    RC522_WriteReg(CommandReg, PCD_Idle);

    // Step 4: Write data to FIFO
    for (i = 0; i < sendLen; i++) {
        RC522_WriteReg(FIFODataReg, sendData[i]);
    }

    // Step 5: Execute command
    RC522_WriteReg(CommandReg, command);
    if (command == PCD_Transceive) {
        RC522_SetBitMask(BitFramingReg, 0x80);   // Start transmission
    }

    // Step 6: Wait for completion (up to 2000 loops ≈ 25ms)
    i = 2000;
    do {
        n = RC522_ReadReg(CommIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

    RC522_ClearBitMask(BitFramingReg, 0x80);     // Stop transmission

    // Step 7: Check for success
    if (i != 0) {
        // Check error register - 0x1B = BufferOvfl | CollErr | CRCErr | ProtocolErr
        if (!(RC522_ReadReg(ErrorReg) & 0x1B)) {
            status = MI_OK;
            
            if (n & irqEn & 0x01) {
                status = MI_NOTAGERR;
            }

            // Step 8: Read received data (for PCD_Transceive)
            if (command == PCD_Transceive) {
                n = RC522_ReadReg(FIFOLevelReg);
                lastBits = RC522_ReadReg(ControlReg) & 0x07;
                
                if (lastBits) {
                    *backLen = (n - 1) * 8 + lastBits;
                } else {
                    *backLen = n * 8;
                }

                if (n == 0) {
                    n = 1;
                }
                if (n > 16) {
                    n = 16;
                }

                for (i = 0; i < n; i++) {
                    backData[i] = RC522_ReadReg(FIFODataReg);
                }
            }
        } else {
            status = MI_ERR;
        }
    } else {
        status = MI_ERR;
    }

    return status;
}

// =================== TRANSCEIVE (Legacy wrapper) ==================
/**
 * Legacy wrapper for backward compatibility with existing code
 */
int RC522_Transceive(uint8_t *send, uint8_t sendLen, uint8_t *back,
                     uint8_t *backLen, uint8_t validBits) {
    
    // Check if we're in authenticated mode (MFCrypto1On bit)
    uint8_t status2 = RC522_ReadReg(Status2Reg);
    uint8_t isAuthenticated = (status2 & 0x08) ? 1 : 0;
    
    // CRITICAL: Only call PCD_Idle if NOT authenticated
    // PCD_Idle destroys the authentication session
    if(!isAuthenticated) {
        RC522_WriteReg(CommandReg, PCD_Idle);
    }
    
    RC522_WriteReg(FIFOLevelReg, 0x80);  // Clear FIFO
    RC522_WriteReg(CommIrqReg, 0x7F);    // Clear all interrupts
    
    // Fill FIFO with data to send
    for(uint8_t i = 0; i < sendLen; i++) {
        RC522_WriteReg(FIFODataReg, send[i]);
    }
    
    // Configure BitFramingReg with valid bits (without 0x80)
    uint8_t bitframing = validBits & 0x07;
    RC522_WriteReg(BitFramingReg, bitframing);
    
    // Start transceive
    RC522_WriteReg(CommandReg, PCD_Transceive);
    
    // SET StartSend bit AFTER starting command
    RC522_SetBitMask(BitFramingReg, 0x80);
    
    // Wait for completion
    uint32_t timeout = 100000;
    uint8_t irq;
    while(timeout--) {
        irq = RC522_ReadReg(CommIrqReg);
        
        // Check if received data (RxIRq=0x20) or timeout (IdleIRq=0x10)
        if(irq & 0x30) break;
        
        delay_us(10);
    }
    
    RC522_ClearBitMask(BitFramingReg, 0x80);  // Clear StartSend
    
    if(timeout == 0) {
        return -1;  // Timeout
    }
    
    // Check errors
    uint8_t error = RC522_ReadReg(ErrorReg);
    // Ignore CollErr (0x08) which can occur during anti-collision
    // Also ignore CRCErr (0x04) in authenticated mode - RC522 handles it
    uint8_t errorMask = isAuthenticated ? 0x1B : 0x13;
    if(error & errorMask) {
        return -1;
    }
    
    // Read data from FIFO
    uint8_t n = RC522_ReadReg(FIFOLevelReg);
    
    // If no data, still valid for some commands
    if(n == 0) {
        *backLen = 0;
        return 0;
    }
    
    if(n > *backLen) {
        n = *backLen;
    }
    
    for(uint8_t i = 0; i < n; i++) {
        back[i] = RC522_ReadReg(FIFODataReg);
    }
    
    *backLen = n;
    return 0;
}

// =================== TRANSCEIVE (ENCRYPTED) ======
// Versión especial que NO rompe la sesión autenticada
int RC522_TransceiveEncrypted(uint8_t *send, uint8_t sendLen, uint8_t *back,
                               uint8_t *backLen, uint8_t validBits) {
    
    // Debug: verificar estado inicial
    uint8_t status2_before = RC522_ReadReg(Status2Reg);
    char dbg[128];
    sprintf(dbg, "    [TransEnc] Status2 inicial: 0x%02X (Crypto=%s)\r\n", 
            status2_before, (status2_before & 0x08) ? "ON" : "OFF");
    USART_SendString(dbg);
    
    // NO llamar a PCD_Idle - esto destruye la autenticación
    // NO limpiar FIFO todavía - puede contener datos de autenticación
    RC522_WriteReg(CommIrqReg, 0x7F);    // Clear todas las interrupciones
    
    sprintf(dbg, "    [TransEnc] Comando: 0x%02X 0x%02X\r\n", send[0], send[1]);
    USART_SendString(dbg);
    
    // Clear FIFO AHORA
    RC522_WriteReg(FIFOLevelReg, 0x80);
    
    // Llenar FIFO con datos a enviar (sin CRC - el RC522 lo hace automático)
    for(uint8_t i = 0; i < sendLen; i++) {
        RC522_WriteReg(FIFODataReg, send[i]);
    }
    
    // BitFramingReg: 0x00 para bytes completos, 0x80 se setea después
    RC522_WriteReg(BitFramingReg, 0x00);
    
    // Iniciar transceive
    RC522_WriteReg(CommandReg, PCD_Transceive);
    
    // SET StartSend bit
    RC522_SetBitMask(BitFramingReg, 0x80);
    
    // Esperar a que termine (timeout más corto)
    uint32_t timeout = 50000;
    uint8_t irq;
    uint32_t loops = 0;
    while(timeout--) {
        irq = RC522_ReadReg(CommIrqReg);
        
        // Terminar cuando reciba datos O timeout
        if(irq & 0x30) break;  // RxIRq(0x20) o IdleIRq(0x10)
        
        loops++;
        delay_us(5);
    }
    
    sprintf(dbg, "    [TransEnc] Esperó %u loops, IRQ final: 0x%02X\r\n", loops, irq);
    USART_SendString(dbg);
    
    RC522_ClearBitMask(BitFramingReg, 0x80);  // Clear StartSend
    
    if(timeout == 0) {
        USART_SendString("    [TransEnc] ✗ TIMEOUT\r\n");
        return -1;
    }
    
    // Verificar errores ANTES de leer FIFO
    uint8_t error = RC522_ReadReg(ErrorReg);
    sprintf(dbg, "    [TransEnc] ErrorReg: 0x%02X ", error);
    USART_SendString(dbg);
    
    // Decodificar bits de error
    if(error & 0x01) USART_SendString("[ProtocolErr] ");
    if(error & 0x02) USART_SendString("[ParityErr] ");
    if(error & 0x04) USART_SendString("[CRCErr] ");
    if(error & 0x08) USART_SendString("[CollErr] ");
    if(error & 0x10) USART_SendString("[BufferOvfl] ");
    USART_SendString("\r\n");
    
    // Para comandos encriptados, ignorar CRCErr ya que el RC522 maneja CRC internamente
    if(error & 0x1B) {  // ProtocolErr, ParityErr, BufferOvfl, CollErr
        USART_SendString("    [TransEnc] ✗ Error crítico\r\n");
        return -1;
    }
    
    // Leer datos del FIFO
    uint8_t n = RC522_ReadReg(FIFOLevelReg);
    sprintf(dbg, "    [TransEnc] FIFO Level: %d bytes\r\n", n);
    USART_SendString(dbg);
    
    if(n == 0) {
        *backLen = 0;
        USART_SendString("    [TransEnc] ⚠ FIFO vacío - tarjeta no respondió\r\n");
        return -1;  // Cambiar a error en vez de éxito
    }
    
    // Leer TODOS los bytes del FIFO (incluye datos + CRC si hay)
    uint8_t actualLen = (n < *backLen) ? n : *backLen;
    for(uint8_t i = 0; i < actualLen; i++) {
        back[i] = RC522_ReadReg(FIFODataReg);
    }
    
    *backLen = actualLen;
    
    // Verificar estado crypto después
    uint8_t status2_after = RC522_ReadReg(Status2Reg);
    sprintf(dbg, "    [TransEnc] Status2 final: 0x%02X (Crypto=%s), Leídos: %d bytes\r\n", 
            status2_after, (status2_after & 0x08) ? "ON" : "OFF", actualLen);
    USART_SendString(dbg);
    
    return 0;
}

// =================== CRC CALCULATION =============
void RC522_CalculateCRC(uint8_t *data, uint8_t len, uint8_t *result) {
    RC522_WriteReg(CommandReg, PCD_Idle);
    RC522_WriteReg(FIFOLevelReg, 0x80);  // Clear FIFO
    
    // Escribir datos al FIFO
    for(uint8_t i = 0; i < len; i++) {
        RC522_WriteReg(FIFODataReg, data[i]);
    }
    
    // Iniciar cálculo CRC
    RC522_WriteReg(CommandReg, 0x03);  // CalcCRC command
    
    // Esperar a que termine
    uint32_t timeout = 5000;
    uint8_t n;
    while(timeout--) {
        n = RC522_ReadReg(CommIrqReg);
        if(n & 0x04) break;  // CRCIRq bit
        delay_us(10);
    }
    
    // Leer resultado (little-endian)
    result[0] = RC522_ReadReg(CRCResultRegL);
    result[1] = RC522_ReadReg(CRCResultRegH);
}

// =================== REQA ========================
int RC522_RequestA(uint8_t *atqa, uint8_t *atqaLen) {
    uint8_t cmd = PICC_REQA;        // 0x26
    uint8_t back[4] = {0};
    uint8_t blen = sizeof(back);
    
    // REQA se env�a con 7 bits v�lidos
    int result = RC522_Transceive(&cmd, 1, back, &blen, 7);
    
    if(result == 0 && blen == 2) {  // Debe recibir exactamente 2 bytes
        atqa[0] = back[0];
        atqa[1] = back[1];
        *atqaLen = 2;
        return 0;
    }
    
    return -1;
}

// =================== ANTICOLLISION ===============
int RC522_AnticollCL1(uint8_t *uid, uint8_t *uidLen) {
    uint8_t cmd[2] = {PICC_ANTICOLL_CL1, 0x20};  // 0x93, 0x20 (SEL, NVB)
    uint8_t back[10] = {0};
    uint8_t blen = sizeof(back);
    int result;
    
    // Anti-colisi�n - se env�a con 0 bits v�lidos (frame de control)
    result = RC522_Transceive(cmd, 2, back, &blen, 0);
    
    if(result == 0 && blen >= 5) {
        // Los primeros 4 bytes son el UID, el quinto es el checksum
        for(uint8_t i = 0; i < 4; i++) {
            uid[i] = back[i];
        }
        uid[4] = back[4];  // BCC (checksum)
        *uidLen = 5;
        return 0;
    }
    
    return -1;
}

// =================== SELECT ======================
int RC522_Select(uint8_t *uid) {
    uint8_t cmd[9];
    uint8_t back[10] = {0};
    uint8_t blen = sizeof(back);
    
    // Comando SELECT: SEL (0x93) + NVB (0x70 = 7 bytes) + UID(4) + BCC(1)
    cmd[0] = PICC_SELECT_CL1;  // 0x93
    cmd[1] = 0x70;             // NVB: 7 bytes de datos (UID + BCC)
    
    // Copiar UID (4 bytes) y BCC (1 byte)
    for(uint8_t i = 0; i < 5; i++) {
        cmd[2 + i] = uid[i];
    }
    
    // Calcular CRC para el SELECT
    uint8_t crcBuf[2];
    RC522_CalculateCRC(cmd, 7, crcBuf);
    cmd[7] = crcBuf[0];
    cmd[8] = crcBuf[1];
    
    // sprintf(tbuf, "SELECT CRC: %02X %02X\r\n", cmd[7], cmd[8]);
    // USART_SendString(tbuf);
    
    int result = RC522_Transceive(cmd, 9, back, &blen, 0);
    
    // sprintf(tbuf, "SELECT result=%d, backLen=%u\r\n", result, blen);
    // USART_SendString(tbuf);
    
    if(result == 0 && blen >= 1) {
        // sprintf(tbuf, "SAK: 0x%02X\r\n", back[0]);
        // USART_SendString(tbuf);
        return 0;
    }
    
    return -1;
}

// =================== AUTENTICAR / LEER / ESCRIBIR BLOQUE ==================
#include <string.h>

// Authenticates to a block using KeyA or KeyB
// authMode: PICC_AUTHENT1A (0x60) or PICC_AUTHENT1B (0x61)
// key: 6-byte key
// uid: pointer to 4-byte UID
// returns 0 on success, -1 on failure
int RC522_Auth(uint8_t authMode, uint8_t blockAddr, uint8_t *key, uint8_t *uid) {
    uint8_t buff[12];

    // DON'T call PCD_Idle here - it destroys the card session!
    // The card is already selected in main, we just need to authenticate
    
    RC522_WriteReg(FIFOLevelReg, 0x80); // clear FIFO

    buff[0] = authMode;
    buff[1] = blockAddr;
    memcpy(&buff[2], key, 6);
    memcpy(&buff[8], uid, 4);

    for(uint8_t i = 0; i < 12; i++) {
        RC522_WriteReg(FIFODataReg, buff[i]);
    }

    RC522_WriteReg(CommandReg, PCD_MFAuthent);

    // Wait for authentication to complete
    uint32_t timeout = 5000;
    while(timeout--) {
        uint8_t n = RC522_ReadReg(CommIrqReg);
        if(n & 0x10) break; // IdleIRq
        delay_us(10);
    }

    // Check Status2Reg for MFCrypto1On bit
    uint8_t status2 = RC522_ReadReg(Status2Reg);
    if(!(status2 & 0x08)) {
        char tbuf[64];
        sprintf(tbuf, "RC522_Auth: failed status2=0x%02X, ErrorReg=0x%02X\r\n", status2, RC522_ReadReg(ErrorReg));
        USART_SendString(tbuf);
        return -1; // authentication failed
    }

    return 0; // success
}

void RC522_StopCrypto1(void) {
    // Clear MFCrypto1On bit
    RC522_ClearBitMask(Status2Reg, 0x08);
}

// Read 16 bytes from blockAddr into data[] (must be 16 bytes); uid is 4-byte UID
// returns 0 on success, negative on error
int RC522_ReadBlock(uint8_t blockAddr, uint8_t *data, uint8_t *uid) {
    uint8_t keyA[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    
    // Autenticar (SELECT debe haberse hecho antes)
    int res = RC522_Auth(PICC_AUTHENT1A, blockAddr, keyA, uid);
    if(res != 0) return -1; // auth failed

    uint8_t cmd[2] = {0x30, blockAddr}; // READ
    uint8_t back[18];
    uint8_t backLen = sizeof(back);
    res = RC522_Transceive(cmd, 2, back, &backLen, 0);
    if(res != 0) {
        char tbuf[96];
        sprintf(tbuf, "RC522_ReadBlock: Transceive READ failed, res=%d, backLen=%u, ErrorReg=0x%02X\r\n", res, backLen, RC522_ReadReg(ErrorReg));
        USART_SendString(tbuf);
        return -2;
    }
    if(backLen < 16) {
        char tbuf[64];
        sprintf(tbuf, "RC522_ReadBlock: short read backLen=%u, FIFOLevel=0x%02X\r\n", backLen, RC522_ReadReg(FIFOLevelReg));
        USART_SendString(tbuf);
        return -3;
    }

    for(uint8_t i = 0; i < 16; i++) data[i] = back[i];

    // Stop authentication (optional) - COMENTADO para permitir múltiples lecturas
    // RC522_WriteReg(CommandReg, PCD_Idle);
    return 0;
}

// Write 16 bytes from data[] into blockAddr; uid is 4-byte UID
// returns 0 on success, negative on error
int RC522_WriteBlock(uint8_t blockAddr, uint8_t *data, uint8_t *uid) {
    uint8_t keyA[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    
    // Autenticar (SELECT debe haberse hecho antes)
    int res = RC522_Auth(PICC_AUTHENT1A, blockAddr, keyA, uid);
    if(res != 0) return -1; // auth failed

    uint8_t cmd[2] = {0xA0, blockAddr}; // WRITE
    uint8_t ack[4];
    uint8_t ackLen = sizeof(ack);
    res = RC522_Transceive(cmd, 2, ack, &ackLen, 0);
    if(res != 0) {
        char tbuf[64];
        sprintf(tbuf, "RC522_WriteBlock: WRITE cmd failed, res=%d, ErrorReg=0x%02X\r\n", res, RC522_ReadReg(ErrorReg));
        USART_SendString(tbuf);
        return -2; // write command failed
    }

    // send 16 bytes of data
    uint8_t back[4];
    uint8_t backLen = sizeof(back);
    res = RC522_Transceive(data, 16, back, &backLen, 0);
    if(res != 0) {
        char tbuf[64];
        sprintf(tbuf, "RC522_WriteBlock: data write failed, res=%d, ErrorReg=0x%02X\r\n", res, RC522_ReadReg(ErrorReg));
        USART_SendString(tbuf);
        return -3; // data write failed
    }

    RC522_WriteReg(CommandReg, PCD_Idle);
    return 0;
}

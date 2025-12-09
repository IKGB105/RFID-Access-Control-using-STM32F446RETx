#ifndef CONF_H
#define CONF_H

#include <stm32f446xx.h>

// ===== Funciones de configuración =====
extern void confRCC(void);
extern void confGPIO(void);
extern void confUSART(void);
extern void confSPI(void);
extern void confALL(void);

#endif

#ifndef UART_HEADER_H
#define UART_HEADER_H


#include "main.h"

void UART_TransmitInt(uint32_t num, USART_TypeDef* USARTx);

void UART_TransmitNum(double num, USART_TypeDef* USARTx);

void UART_TransmitStr(char* str, USART_TypeDef* USARTx);

void UART_TransmitNewLine(USART_TypeDef* USARTx);


#endif

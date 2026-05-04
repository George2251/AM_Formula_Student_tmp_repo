
#include "UART_header.h"



void UART_TransmitInt(uint32_t num, USART_TypeDef* USARTx)
{
	if(num == 0)
	{
		while(!LL_USART_IsActiveFlag_TXE(USARTx))
		{}
		LL_USART_TransmitData8(USARTx, '0');
		return;
	}

	uint32_t tmp = 0;
	uint8_t count = 0;
	while(num != 0)
	{
		count++;
		tmp *= 10;
		tmp += num%10;
		num /= 10;
	}

	while(count != 0)
	{
		count--;
		while(!LL_USART_IsActiveFlag_TXE(USARTx))
				{}


		LL_USART_TransmitData8(USARTx, (tmp%10) + 48);
		tmp /= 10;
	}
}

void UART_TransmitNum(double num, USART_TypeDef* USARTx)
{
	uint32_t intNum = (uint32_t)num;
	UART_TransmitInt(intNum, USARTx);
	UART_TransmitStr(".", USARTx);
	num -= intNum;							//get the fraction part
	UART_TransmitInt((uint32_t)(num*1000), USARTx);		//multiply by 1000 to get the first 3-decimal digits after the decimal point
}


void UART_TransmitStr(char* str, USART_TypeDef* USARTx)
{
	while(*str != '\0')
	{
		if(*str == '\n')
		{
			UART_TransmitNewLine(USARTx);
		}
		else
		{
			while(!LL_USART_IsActiveFlag_TXE(USARTx))
			{}
			LL_USART_TransmitData8(USARTx, *str);
		}
		str++;
	}
}


void UART_TransmitNewLine(USART_TypeDef* USARTx)
{
	while(!LL_USART_IsActiveFlag_TXE(USARTx))
			{}

	LL_USART_TransmitData8(USARTx, '\r');
	while(!LL_USART_IsActiveFlag_TXE(USARTx))
				{}

	LL_USART_TransmitData8(USARTx, '\n');
}

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "UART_header.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint8_t usr_req = 0; //1: gear status, 2: sensor data
volatile uint32_t timeout_counter = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define USE_TIMEOUT
#define DEBUGING
#define DEBUGING_USART USART2

#define HDS_KLINE_STAT_RECIEVING		0
#define HDS_KLINE_STAT_SENDING 			1
#define HDS_KLINE_STAT_WORKING			2
#define HDS_KLINE_STAT_WAITING			3

//#define TRANSMIT_DISABLE_RX
#define TRANSMIT_DUMP_RX_DATA

uint8_t hds_kline_status = 0;

uint16_t hds_timeout_ms = 1;

#define HDS_KLINE_TIMEOUT_MS	2000

#define HDS_KLINE_COMMUNICATION_UART 	USART1

#define HDS_WAKE_UP_MSG_LENGTH 				4
#define HDS_INIT_MSG_LENGTH					5
#define HDS_INIT_RESPONSE_MSG_LENGTH 		4
#define HDS_REQUEST_MSG_LENGTH 				5

//These needs to be tested
#define HDS_REQUEST_STATUS_GEAR_TABLE			0xD1
#define HDS_REQUEST_STATUS_GEAR_CHECKSUM 		0x47

#define HDS_REQUEST_STATUS_GEAR_GEAR_STATUS		4
#define HDS_REQUEST_STATUS_GEAR_ENGINE_STATUS	8

#define HDS_REQUEST_SENSOR_DATA_TABLE			0x10
#define HDS_REQUEST_SENSOR_DATA_CHECKSUM 		0x08

#define HDS_REQUEST_SENSOR_DATA_RPM_HIGH		4
#define HDS_REQUEST_SENSOR_DATA_RPM_LOW			5
#define HDS_REQUEST_SENSOR_DATA_TPS_VOLTAGE		6
#define HDS_REQUEST_SENSOR_DATA_TPS_PERCENTAGE	7
#define HDS_REQUEST_SENSOR_DATA_ECT_VOLTAGE		8
#define HDS_REQUEST_SENSOR_DATA_ECT_DEGC		9
#define HDS_REQUEST_SENSOR_DATA_IAT_VOLTAGE		10
#define HDS_REQUEST_SENSOR_DATA_IAT_DEGC		11
#define HDS_REQUEST_SENSOR_DATA_MAP_VOLTAGE		12
#define HDS_REQUEST_SENSOR_DATA_MAP_KPA			13
#define HDS_REQUEST_SENSOR_DATA_BATTERY_VOLTAGE	16
#define HDS_REQUEST_SENSOR_DATA_SPEED_KM_H		17



#define HDS_BUFFER_MAX_SIZE		25

uint8_t hds_wake_up_msg[HDS_WAKE_UP_MSG_LENGTH] = { 0xFE, 0x04, 0xFF, 0xFF};
uint8_t hds_init_msg[HDS_INIT_MSG_LENGTH] = {0x72, 0x05, 0x00, 0xF0, 0x99};
uint8_t	hds_init_response_msg[HDS_INIT_RESPONSE_MSG_LENGTH] = {0x02, 0x04, 0x00, 0xFA};
uint8_t hds_request_msg[HDS_REQUEST_MSG_LENGTH] = {0x72, 0x05, 0x71, 0x10, 0x47};

uint8_t hds_buffer[HDS_BUFFER_MAX_SIZE]= {};

void HDS_KLine_init()
{
	uint8_t hds_msg_counter = 0;

#ifdef TRANSMIT_DISABLE_RX
	LL_USART_DisableDirectionRx(HDS_KLINE_COMMUNICATION_UART);
#endif
	LL_USART_Disable(HDS_KLINE_COMMUNICATION_UART);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_OUTPUT);

	LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_9);
	LL_mDelay(70);

	LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_9);
	LL_mDelay(120);

	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_ALTERNATE);
	LL_USART_Enable(HDS_KLINE_COMMUNICATION_UART);

	hds_msg_counter = 0;
	SET_BIT(hds_kline_status, HDS_KLINE_STAT_SENDING);
	while(hds_msg_counter != HDS_WAKE_UP_MSG_LENGTH)
	{
		while(!LL_USART_IsActiveFlag_TXE(HDS_KLINE_COMMUNICATION_UART))
		{}
		LL_USART_TransmitData8(HDS_KLINE_COMMUNICATION_UART, hds_wake_up_msg[hds_msg_counter]);
#ifdef TRANSMIT_DUMP_RX_DATA
		while(!LL_USART_IsActiveFlag_RXNE(HDS_KLINE_COMMUNICATION_UART)){}
		uint8_t dummy = LL_USART_ReceiveData8(HDS_KLINE_COMMUNICATION_UART);
#endif
		hds_msg_counter++;
	}
	CLEAR_BIT(hds_kline_status, HDS_KLINE_STAT_SENDING);

	LL_mDelay(200);

	hds_msg_counter = 0;
	SET_BIT(hds_kline_status, HDS_KLINE_STAT_SENDING);
	while(hds_msg_counter != HDS_INIT_MSG_LENGTH)
	{
		while(!LL_USART_IsActiveFlag_TXE(HDS_KLINE_COMMUNICATION_UART))
		{}
		LL_USART_TransmitData8(HDS_KLINE_COMMUNICATION_UART, hds_init_msg[hds_msg_counter]);
#ifdef TRANSMIT_DUMP_RX_DATA
		while(!LL_USART_IsActiveFlag_RXNE(HDS_KLINE_COMMUNICATION_UART)){}
		uint8_t dummy = LL_USART_ReceiveData8(HDS_KLINE_COMMUNICATION_UART);
#endif
		hds_msg_counter++;
	}
	CLEAR_BIT(hds_kline_status, HDS_KLINE_STAT_SENDING);


#ifdef DEBUGING
	UART_TransmitStr("Waiting for the ECU to respond\n", DEBUGING_USART);
#endif

#ifdef USE_TIMEOUT
	timeout_counter = HDS_KLINE_TIMEOUT_MS;
#endif

#ifdef TRANSMIT_DISABLE_RX
	LL_USART_EnableDirectionRx(HDS_KLINE_COMMUNICATION_UART);
#endif


	SET_BIT(hds_kline_status, HDS_KLINE_STAT_RECIEVING);
	hds_msg_counter = 0;
	while(hds_msg_counter != HDS_INIT_RESPONSE_MSG_LENGTH)
	{
		while(!LL_USART_IsActiveFlag_RXNE(HDS_KLINE_COMMUNICATION_UART))
		{
#ifdef USE_TIMEOUT
			if(timeout_counter == 0)
			{
				return;
			}
#endif
		}

		hds_buffer[hds_msg_counter] = LL_USART_ReceiveData8(HDS_KLINE_COMMUNICATION_UART);
		LL_USART_TransmitData8(DEBUGING_USART, hds_buffer[hds_msg_counter]);
		hds_msg_counter++;
	}

	if((hds_buffer[0] == hds_init_response_msg[0])
	&& (hds_buffer[1] == hds_init_response_msg[1])
	&& (hds_buffer[2] == hds_init_response_msg[2])
	&& (hds_buffer[3] == hds_init_response_msg[3]))
	{
		UART_TransmitStr("connected", DEBUGING_USART);
				  UART_TransmitNewLine(DEBUGING_USART);

		SET_BIT(hds_kline_status, HDS_KLINE_STAT_WORKING);
	}
}


void HDS_KLine_request(uint8_t hds_table_ID, uint8_t hds_msg_checksum)
{
	uint8_t hds_msg_counter = 0;

	//set the table to be requested
	hds_request_msg[3] = hds_table_ID;
	//set the checksum of the message(for now it is constant
	//but can be made to be calculated later)
	hds_request_msg[4] = hds_msg_checksum;

#ifdef TRANSMIT_DISABLE_RX
	LL_USART_DisableDirectionRx(HDS_KLINE_COMMUNICATION_UART);
#endif

	hds_msg_counter = 0;
	SET_BIT(hds_kline_status, HDS_KLINE_STAT_SENDING);
	while(hds_msg_counter != HDS_REQUEST_MSG_LENGTH)
	{
		while(!LL_USART_IsActiveFlag_TXE(HDS_KLINE_COMMUNICATION_UART))
		{}
		LL_USART_TransmitData8(HDS_KLINE_COMMUNICATION_UART, hds_request_msg[hds_msg_counter]);
#ifdef TRANSMIT_DUMP_RX_DATA
		while(!LL_USART_IsActiveFlag_RXNE(HDS_KLINE_COMMUNICATION_UART)){}
		uint8_t dummy = LL_USART_ReceiveData8(HDS_KLINE_COMMUNICATION_UART);
#endif
		hds_msg_counter++;
	}
	CLEAR_BIT(hds_kline_status, HDS_KLINE_STAT_SENDING);

#ifdef TRANSMIT_DISABLE_RX
	LL_USART_EnableDirectionRx(HDS_KLINE_COMMUNICATION_UART);
#endif	LL_USART_ReceiveData8(HDS_KLINE_COMMUNICATION_UART);


	SET_BIT(hds_kline_status, HDS_KLINE_STAT_WAITING);

}

void HDS_KLine_recieve()
{

	uint8_t hds_msg_counter = 0;
	uint8_t hds_msg_length = 2; //just an initial value and will be replaced by the message length later

#ifdef DEBUGING
	UART_TransmitStr("Waiting for the ECU to respond to request\n", DEBUGING_USART);
#endif

#ifdef USE_TIMEOUT
	timeout_counter = HDS_KLINE_TIMEOUT_MS;
#endif

	SET_BIT(hds_kline_status, HDS_KLINE_STAT_RECIEVING);
	hds_msg_counter = 0;
	while(hds_msg_counter != hds_msg_length)
	{
		while(!LL_USART_IsActiveFlag_RXNE(HDS_KLINE_COMMUNICATION_UART))
		{
#ifdef USE_TIMEOUT
			if(timeout_counter == 0)
			{
				return;
			}

#endif
		}

		hds_buffer[hds_msg_counter] = LL_USART_ReceiveData8(HDS_KLINE_COMMUNICATION_UART);

		hds_msg_counter++;
		if(hds_msg_counter == 2)
		{
			hds_msg_length = hds_buffer[1];
			if(hds_msg_length > HDS_BUFFER_MAX_SIZE)
			{
				hds_msg_length = HDS_BUFFER_MAX_SIZE;
			}
		}
	}

	CLEAR_BIT(hds_kline_status, HDS_KLINE_STAT_WAITING);

	//printing logic to the debugging uart here

	UART_TransmitStr("HDS: message length: ", DEBUGING_USART);
	UART_TransmitNum(hds_msg_length, DEBUGING_USART);
	UART_TransmitNewLine(DEBUGING_USART);
	for(int i = 0; i < hds_msg_length; i++)
	{
		UART_TransmitStr("HDS: byte no: ", DEBUGING_USART);
		UART_TransmitNum(i, DEBUGING_USART);
		UART_TransmitStr(", value: ", DEBUGING_USART);
		UART_TransmitNum(hds_buffer[i], DEBUGING_USART);
		UART_TransmitNewLine(DEBUGING_USART);
	}

}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_AFIO);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

  /* System interrupt init*/
  NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

  /* SysTick_IRQn interrupt configuration */
  NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),15, 0));

  /** NOJTAG: JTAG-DP Disabled and SW-DP Enabled
  */
  LL_GPIO_AF_Remap_SWJ_NOJTAG();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  LL_USART_EnableDirectionRx(HDS_KLINE_COMMUNICATION_UART);
  LL_USART_EnableDirectionTx(HDS_KLINE_COMMUNICATION_UART);
  LL_USART_Enable(HDS_KLINE_COMMUNICATION_UART);

  LL_USART_EnableDirectionRx(DEBUGING_USART);
  LL_USART_EnableDirectionTx(DEBUGING_USART);
  LL_USART_EnableIT_RXNE(DEBUGING_USART);
  LL_USART_Enable(DEBUGING_USART);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  if(!READ_BIT(hds_kline_status, HDS_KLINE_STAT_WORKING))
	  {
		  UART_TransmitNewLine(DEBUGING_USART);
		  UART_TransmitStr("Initializing the Kline", DEBUGING_USART);
		  UART_TransmitNewLine(DEBUGING_USART);
		  HDS_KLine_init();

	  }
	  else
	  {
		  switch(usr_req)
		  {
		  case '1':
			  UART_TransmitStr("requesting status gear table", DEBUGING_USART);
			  UART_TransmitNewLine(DEBUGING_USART);
			  HDS_KLine_request(HDS_REQUEST_STATUS_GEAR_TABLE, HDS_REQUEST_STATUS_GEAR_CHECKSUM);
			  HDS_KLine_recieve();
			  break;
		  case '2':
			  UART_TransmitStr("requesting sensor data table", DEBUGING_USART);
			  UART_TransmitNewLine(DEBUGING_USART);
			  HDS_KLine_request(HDS_REQUEST_SENSOR_DATA_TABLE, HDS_REQUEST_SENSOR_DATA_CHECKSUM);
			  HDS_KLine_recieve();
			  break;
		  default:
			  break;
		  }

		  usr_req = '0';
	  }

	  LL_mDelay(200);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_2);
  while(LL_FLASH_GetLatency()!= LL_FLASH_LATENCY_2)
  {
  }
  LL_RCC_HSE_Enable();

   /* Wait till HSE is ready */
  while(LL_RCC_HSE_IsReady() != 1)
  {

  }
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE_DIV_1, LL_RCC_PLL_MUL_9);
  LL_RCC_PLL_Enable();

   /* Wait till PLL is ready */
  while(LL_RCC_PLL_IsReady() != 1)
  {

  }
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
  {

  }
  LL_Init1msTick(72000000);
  LL_SetSystemCoreClock(72000000);
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */
  /* USER CODE END USART1_Init 0 */

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA);
  /**USART1 GPIO Configuration
  PA9   ------> USART1_TX
  PA10   ------> USART1_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_10;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_FLOATING;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  USART_InitStruct.BaudRate = 10400;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(USART1);
  LL_USART_Enable(USART1);
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA);
  /**USART2 GPIO Configuration
  PA2   ------> USART2_TX
  PA3   ------> USART2_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_FLOATING;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USART2 interrupt Init */
  NVIC_SetPriority(USART2_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(USART2_IRQn);

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART2, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(USART2);
  LL_USART_Enable(USART2);
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOD);
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA);

  /**/
  LL_GPIO_ResetOutputPin(Out_Test_GPIO_Port, Out_Test_Pin);

  /**/
  GPIO_InitStruct.Pin = Out_Test_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  LL_GPIO_Init(Out_Test_GPIO_Port, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = In_Test_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_FLOATING;
  LL_GPIO_Init(In_Test_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

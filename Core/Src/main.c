/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */

//Examples 13-15  => Timers

/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
//####################################################################################
/* FreeRTOS.org includes. */
#include "FreeRTOS.h"//configUSE_TIMERS is set to 1 in FreeRTOSConfig.h
#include "task.h"
#include "timers.h"//Include software timer functionality
#include "queue.h"
#include "semphr.h"
#include <time.h> //generating random number
#include <stdlib.h>
#include<stdio.h>//printf
#include <string.h>
//####################################################################################
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

ETH_HandleTypeDef heth;

UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

//#######################################################################
/* Declare a variable of type SemaphoreHandle_t.  This is used to reference the
mutex type semaphore that is used to ensure mutual exclusive access to stdout. */
SemaphoreHandle_t xMutex;

// for printf =============================================
int __io_putchar(int ch)
{
 /* Place your implementation of fputc here */
 /* e.g. write a character to the USART2 and Loop until the end of transmission */
//	xSemaphoreTake( xMutex, portMAX_DELAY );

    HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, 0xFFFF);

//	xSemaphoreGive(xMutex);

return ch;
}


#define mainAUTO_RELOAD_TIMER_PERIOD ( pdMS_TO_TICKS( 10000UL ) )
/*-----------------------------------------------------------*//*
 * The callback function that is used by random timer.
 */
static void randomTimerCallback( TimerHandle_t xTimer );

/*-----------------------------------------------------------*/
/* The timer handle is used inside the callback function so this time is
given file scope (global). */
static TimerHandle_t xAutoReloadTimer;

//Queue sender(Timer ISR delegated) and receiver(dispatcher) tasks
static void vQSenderISRdelegatedTask( void *pvParameters );
static void vQReceiverDispatcherTask( void *pvParameters );

//Services tasks
static void vPoliceTask( void *pvParameters );
static void vFireSquadTask( void *pvParameters );
static void vAmbulanceTask( void *pvParameters );
static void vCoronaTask( void *pvParameters );


#define MAX_CARS_UNITS 5
#define SERVICES_COUNT 4


//Globals
uint8_t max_concurrent[SERVICES_COUNT]={3, 2, 4, 4};
uint16_t interrupt_count={0}; //This global is updated by timer ISR and read by ISR delegated task. Used for preventing missing interrupts handling in high frequency rate (solving issue where- timer ISR giving binary semaphore to sync with delegated task when semaphore is already given, leading for missing interrupt handling. for more details see p.233 in "161204_Mastering_the_FreeRTOS_Real_Time_Kernel-A_Hands-On_Tutorial_Guide.pdf")
uint32_t service_cars_to_send[SERVICES_COUNT]={0}; //Accumulates the number of cars to send for all services and from all interrupts up to current time
static const char* serviceTaskName[]={"POLICE_", "FIRE-SQUAD_", "AMBULANCE_", "CORONA_"};
static char fullServiceTaskName[SERVICES_COUNT*MAX_CARS_UNITS][20]={0};

//function pointer array to point to each service task function
static void(*pfService[MAX_CARS_UNITS])(void*);// = {vPoliceTask, vFireSquadTask, vAmbulanceTask, vCoronaTask};

typedef enum {
	POLICE,
	FIRE_SQUAD,
	AMBULANCE,
	CORONA
} Service;

typedef struct{
	Service service;
	uint8_t cars_to_send;
}Message_t;


QueueHandle_t xQueue;

/* Declare a variable of type SemaphoreHandle_t.  This is used to reference the
semaphore that is used to synchronise a task with an interrupt. */
SemaphoreHandle_t xSendBinarySemaphore;
SemaphoreHandle_t xReceiveBinarySemaphore;

//Concurrent number of cars servicing limiting semaphore
SemaphoreHandle_t xPolice_CountingSemaphore;
SemaphoreHandle_t xFire_CountingSemaphore;
SemaphoreHandle_t xAmbulance_CountingSemaphore;
SemaphoreHandle_t xCorona_CountingSemaphore;

///* Declare 10 variables of type Message_t that will be passed on the queue. */
//static const Message_t xStructsToSend[10] ={0};
//static int nextReadIndx=0;

//#######################################################################
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
	BaseType_t xTimerStarted;


	pfService[POLICE] = vPoliceTask;
	pfService[FIRE_SQUAD] =vFireSquadTask;
	pfService[AMBULANCE] =vAmbulanceTask;
	pfService[CORONA] =vCoronaTask;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  //MX_ETH_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  xMutex = xSemaphoreCreateMutex(); //used for printf

  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* Before a semaphore is used it must be explicitly created.  In this
	example	a binary semaphore is created. */
    xSendBinarySemaphore = xSemaphoreCreateBinary();
    xReceiveBinarySemaphore = xSemaphoreCreateBinary();

    xPolice_CountingSemaphore = xSemaphoreCreateCounting(max_concurrent[POLICE],0);
    xFire_CountingSemaphore = xSemaphoreCreateCounting(max_concurrent[FIRE_SQUAD],0);
    xAmbulance_CountingSemaphore = xSemaphoreCreateCounting(max_concurrent[AMBULANCE],0);
    xCorona_CountingSemaphore = xSemaphoreCreateCounting(max_concurrent[CORONA],0);

	if(xSendBinarySemaphore == NULL || xReceiveBinarySemaphore == NULL ||  xPolice_CountingSemaphore == NULL ||
	   xFire_CountingSemaphore == NULL ||  xAmbulance_CountingSemaphore == NULL ||  xCorona_CountingSemaphore)
	{
		//TODO: log and exit
	}

  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  //################################################################################
  /* Create the auto-reload, storing the handle to the created timer in
  	xAutoReloadTimer. */
  	xAutoReloadTimer = xTimerCreate( "RandomAutoReload",					/* Text name for the timer - not used by FreeRTOS. */
  									 mainAUTO_RELOAD_TIMER_PERIOD,	/* The timer's period in ticks. I this project it is only relevant for first interrupt (because in the ISR a call to xTimerChangePeriodFromISR() is made)*/
  									 pdTRUE,						/* Set uxAutoRealod to pdTRUE to create an auto-reload timer. */
  									 0,								/* The timer ID is initialised to 0. */
									 randomTimerCallback );			/* The callback function to be used by the timer being created. */



  	/* Check the timers were created. */
  		if( xAutoReloadTimer != NULL)
  		{
  			/* Start the timer, using a block time of 0 (no block time).  The
  			scheduler has not been started yet so any block time specified here
  			would be ignored anyway. */
  			xTimerStarted = xTimerStart( xAutoReloadTimer, 0 );

  			/* The implementation of xTimerStart() uses the timer command queue, and
  			xTimerStart() will fail if the timer command queue gets full.  The timer
  			service task does not get created until the scheduler is started, so all
  			commands sent to the command queue will stay in the queue until after
  			the scheduler has been started.  Check both calls to xTimerStart()
  			passed. */
  			if( xTimerStarted == pdPASS )
  			{
  				/* Start the scheduler. */
  				//vTaskStartScheduler();
  			}

  			srand((unsigned int)time(NULL));  // Initialization, should only be called once.
  		}
  	//##########################################################################
  	/* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  		//This xQueue is used to store the queue that is accessed by tasks:
  		//sender to queue: Timer ISR
  		//receiver frpm the queue: Dispatcher
  		//Note: No need to add semaphores to synchronize the tasks reading/writing since xQueue has it's own internal semaphores
  	    xQueue = xQueueCreate(10, sizeof(Message_t));

  		if( xQueue != NULL )
  		{
  			/* Create two instances of the task that will write to the queue.  The
  			parameter is used to pass the value that the task should write to the queue,
  			so one task will continuously write 100 to the queue while the other task
  			will continuously write 200 to the queue.  Both tasks are created at
  			priority 1. */
  			xTaskCreate( vQSenderISRdelegatedTask, "ISRDelegatedQSender", 1000, NULL, 3, NULL );

  			/* Create the task that will read from the queue.  The task is created with
  			priority 2, so above the priority of the sender tasks.
  			Noga: Receiver should have higher priority, WHY?
  			1) If queue is empty than the receiver blocks. if an item inserted to queue than the receiver unblocks and enter
  			the READY state. so for the receiver to move imidiately to the RUNNING state and read the single item (keep the queue containing only 1 item)
  			than the receiver should preemt the senders tasks and so it must have higher priority than them in order to do that!
  			2) since there are 2 senders vs 1 receiver and senders block time is 0(no block),
  			the senders will fill up the queue quicker than receiver reads and very soon the process will get stuck because the
  			queue will stay filled by senders. senders write continouesly without blocking, so receiver must have higher priority!
  			NOTE that receiver must block to give senders the chance to send data since it has higher priority*/
  			xTaskCreate( vQReceiverDispatcherTask, "QReceiverDispatcher", 1000, NULL, 2, NULL );

  			//xTaskCreate( vFireSquadTask, "FIRE_1", 1000, (void*)"FIRE_1", 1, NULL );
  			//xTaskCreate( vAmbulanceTask, "AMBULANCE_1", 1000, (void*)"AMBULANCE_1", 1, NULL );

  			xTaskCreate( pfService[0], fullServiceTaskName[0], 1000, (void*)"POLICE_1", 1, NULL );
  			xTaskCreate( pfService[1], fullServiceTaskName[1], 1000, (void*)"POLICE_2", 1, NULL );
  			xTaskCreate( pfService[2], fullServiceTaskName[2], 1000, (void*)"POLICE_3", 1, NULL );
  			//char taskName[20];
  			char carNum[2];

//  			for(int i = 0 ; i < SERVICES_COUNT ; ++i){
//  				for(int j = 0 ; j < MAX_CARS_UNITS ; ++j){
//                    strcpy(fullServiceTaskName[i+j], serviceTaskName[i]);
//                    carNum[0] = '0'+j+1;
//                    carNum[1]='\0';
//                    strcat(fullServiceTaskName[i+j], (const char*)&carNum);
//
//                	xSemaphoreTake( xMutex, portMAX_DELAY );
//  					printf("creating task: %s, i= %d, j=%d\r\n", fullServiceTaskName[i+j], i, j);
//  					xSemaphoreGive( xMutex);
//
//  		  			if(xTaskCreate( pfService[i], fullServiceTaskName[i+j], 1000, (void*)fullServiceTaskName[i+j], 1, NULL ) == pdFAIL)
//  		  			{
//  		  			    xSemaphoreTake( xMutex, portMAX_DELAY );
//  		  				printf("FAILED creating task %s\r\n", fullServiceTaskName[i+j]);
//  	  					xSemaphoreGive( xMutex);
//
//  		  			}
//  		  			else
//  		  			{
//  	                	xSemaphoreTake( xMutex, portMAX_DELAY );
//  		  				printf("Successfully created task %s\r\n", fullServiceTaskName[i+j]);
//  	  					xSemaphoreGive( xMutex);
//
//  		  			}
//  			 }
  			//}

  			/* Start the scheduler so the created tasks start executing. */
  			vTaskStartScheduler();
  		}
  		else
  		{
  			/* The queue could not be created. */
  		}

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  //defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes); //Is that needed? Is that Idle?

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  //osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 72;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART3|RCC_PERIPHCLK_CLK48;
  PeriphClkInitStruct.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
  PeriphClkInitStruct.Clk48ClockSelection = RCC_CLK48SOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{

  /* USER CODE BEGIN ETH_Init 0 */

  /* USER CODE END ETH_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH_Init 1 */

  /* USER CODE END ETH_Init 1 */
  heth.Instance = ETH;
  heth.Init.AutoNegotiation = ETH_AUTONEGOTIATION_ENABLE;
  heth.Init.Speed = ETH_SPEED_100M;
  heth.Init.DuplexMode = ETH_MODE_FULLDUPLEX;
  heth.Init.PhyAddress = LAN8742A_PHY_ADDRESS;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.RxMode = ETH_RXPOLLING_MODE;
  heth.Init.ChecksumMode = ETH_CHECKSUM_BY_HARDWARE;
  heth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ETH_Init 2 */

  /* USER CODE END ETH_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 6;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|LD3_Pin|LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port, USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_Btn_Pin */
  GPIO_InitStruct.Pin = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD1_Pin LD3_Pin LD2_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|LD3_Pin|LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_OverCurrent_Pin */
  GPIO_InitStruct.Pin = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
//################################################################
//Timer ISR delegated task -> In order for this delegated function to run immidiately after ISR finish, it must have the highest priority of all tasks
//Sender priority is set to the highest task in order to get executed right after ISR finishes.
//It will send msg to queue with waiting parameter so that it is blocked if the queue is full (enabling vQReceiverTask to execute)
static void vQSenderISRdelegatedTask( void *pvParameters )
{
	BaseType_t xStatus;
	const TickType_t xTicksToWait = pdMS_TO_TICKS( 100UL );

	/* xMaxExpectedBlockTime holds the maximum time expected between two interrupts. */
	const TickType_t xMaxExpectedBlockTime = pdMS_TO_TICKS(7000 );//TODO: configuration

	Message_t message;

  	xSemaphoreTake( xMutex, portMAX_DELAY );
		printf("ISR Delegated Q-sender STARTS!\r\n");
		xSemaphoreGive( xMutex);


		/* As per most tasks, this task is implemented within an infinite loop. */
		for( ;; )
		{
			/* Use the semaphore to wait for the event.  The semaphore was created
			before the scheduler was started so before this task ran for the first
			time.  The task blocks indefinitely meaning this function call will only
			return once the semaphore has been successfully obtained - so there is
			no need to check the returned value. */
			if(xSemaphoreTake( xSendBinarySemaphore, xMaxExpectedBlockTime )== pdPASS) //Setting xTicksToWait to 'portMAX_DELAY' will cause the task to wait indefinitely(without a timeout) if INCLUDE_vTaskSuspend is set to 1 in FreeRTOSConfig.h.      //NOTE!: 'portMAX_DELAY' results in the task waiting indefinitely(without a time out) for the semaphore to be available.Indefinite timeouts are often used in example code.However, indefinite timeouts are normally bad practice in real applications, because they make it difficult to recover from an error.
			{
            	xSemaphoreTake( xMutex, portMAX_DELAY );
				printf("ISR Delegated Q-sender took Semaphore\r\n");
				xSemaphoreGive( xMutex);

				while(interrupt_count)
				{
					message.service = rand()%SERVICES_COUNT; //0-3
					message.cars_to_send = rand()% max_concurrent[message.service] + 1;
					/* The first parameter is the queue to which data is being sent.  The
					queue was created before the scheduler was started, so before this task
					started to execute.

					The second parameter is the address of the structure being sent.  The
					address is passed in as the task parameter.

					The third parameter is the Block time - the time the task should be kept
					in the Blocked state to wait for space to become available on the queue
					should the queue already be full.  A block time is specified as the queue
					will become full.  Items will only be removed from the queue when both
					sending tasks are in the Blocked state.. */
					xStatus = xQueueSendToBack( xQueue, (void*)&message, xTicksToWait );

					xSemaphoreTake( xMutex, portMAX_DELAY );
					printf("interrupt_count = %u\r\n", interrupt_count);
  					xSemaphoreGive( xMutex);



					--interrupt_count;
					if( xStatus != pdPASS )
					{
						/* We could not write to the queue because it was full - this must
						be an error as the receiving task should make space in the queue
						as soon as both sending tasks are in the Blocked state. */
	                	xSemaphoreTake( xMutex, portMAX_DELAY );
						printf( "Could not send to the queue.\r\n" );
	  					xSemaphoreGive( xMutex);

					}
				}
			}
			else
			{
				/* An event was not received within the expected time. Check for, and if
				 necessary clear, any error conditions that might be
				 preventing the generation of any more interrupts. */
				 //ClearErrors();
			}

		}
}

static void vQReceiverDispatcherTask( void *pvParameters )
{
	/* Declare the structure that will hold the values received from the queue. */
	Message_t xReceivedStructure;
	BaseType_t xStatus;
	const TickType_t xMaxExpectedBlockTime = pdMS_TO_TICKS(7000 );//TODO: configuration

            xSemaphoreTake( xMutex, portMAX_DELAY );
            printf("vQReceiverDispatcherTask STARTS");
			  xSemaphoreGive( xMutex);

		/* This task is also defined within an infinite loop. */
		for( ;; )
		{
			//xSemaphoreTake( xReceiveBinarySemaphore, portMAX_DELAY );  //Setting xTicksToWait to portMAX_DELAY will cause the task to wait

			/* As this task only runs when the sending tasks are in the Blocked state,
			and the sending tasks only block when the queue is full, this task should
			always find the queue to be full.  3 is the queue length. */
//			if( uxQueueMessagesWaiting( xQueue ) != 10 )
//			{
//				printf( "Queue should have been full!\r\n" );
//			}

			/* The first parameter is the queue from which data is to be received.  The
			queue is created before the scheduler is started, and therefore before this
			task runs for the first time.

			The second parameter is the buffer into which the received data will be
			placed.  In this case the buffer is simply the address of a variable that
			has the required size to hold the received structure.

			The last parameter is the block time - the maximum amount of time that the
			task should remain in the Blocked state to wait for data to be available
			should the queue already be empty.  A block time is not necessary as this
			task will only run when the queue is full so data will always be available. */
			xStatus = xQueueReceive( xQueue, &xReceivedStructure, xMaxExpectedBlockTime );

			if( xStatus == pdPASS )
			{
				switch(xReceivedStructure.service)
				{
				 case POLICE:
	                  xSemaphoreTake( xMutex, portMAX_DELAY );
		              printf("Received from Queue =>  %u POLICE cars\r\n", xReceivedStructure.cars_to_send);
	  				  xSemaphoreGive( xMutex);

		              service_cars_to_send[POLICE] += xReceivedStructure.cars_to_send;
		              for(int i = 0 ; i < xReceivedStructure.cars_to_send ; ++i)
		              {
		            	 // printf("Giving semaphore to POLICE\r\n");
		            	  if(xSemaphoreGive(xPolice_CountingSemaphore) == pdFAIL) //semaphore may exceed the semaphore count so no semaphore will be given, but the number of calls is save in 'xReceivedStructure.cars_to_send' so no interrupt will be ignored
		            	  {

		            	  }
		            		  //printf("FAIL giving semaphore to police\r\n");
		            	  // printf("Gave semaphore to POLICE\r\n");
		              }
		              break;
				 case FIRE_SQUAD:
	                  xSemaphoreTake( xMutex, portMAX_DELAY );
		              printf("Received from Queue =>  %u FIRE-SQUAD cars\r\n", xReceivedStructure.cars_to_send);
	  				  xSemaphoreGive( xMutex);

		              service_cars_to_send[FIRE_SQUAD] += xReceivedStructure.cars_to_send;
		              for(int i = 0 ; i < xReceivedStructure.cars_to_send ; ++i)
		              {
		            	//  printf("Giving semaphore to FIRE_SQUAD\r\n");
		            	  if(xSemaphoreGive(xFire_CountingSemaphore) == pdFAIL)
		            	  {

		            	  }
			            	 // printf("FAIL giving semaphore to FIRE\r\n");

		            	//  printf("Gave semaphore to FIRE_SQUAD\r\n");

		              }
			          break;
				 case AMBULANCE:
	                  xSemaphoreTake( xMutex, portMAX_DELAY );
		              printf("Received from Queue =>  %u AMBULANCE cars\r\n", xReceivedStructure.cars_to_send);
	  				  xSemaphoreGive( xMutex);

		              service_cars_to_send[AMBULANCE] += xReceivedStructure.cars_to_send;
		              for(int i = 0 ; i < xReceivedStructure.cars_to_send ; ++i)
		              {
		            	//  printf("Giving semaphore to AMBULANCE\r\n");
		            	  if(xSemaphoreGive(xAmbulance_CountingSemaphore) == pdFAIL)
		            	  {

		            	  }
			            	    // printf("FAIL giving semaphore to AMBULANCE\r\n");

		            	//  printf("Gave semaphore to AMBULANCE\r\n");

		              }
			          break;
			     case CORONA:
	                  xSemaphoreTake( xMutex, portMAX_DELAY );
				      printf("Received from Queue =>  %u CORONA cars\r\n", xReceivedStructure.cars_to_send);
	  				  xSemaphoreGive( xMutex);

				      service_cars_to_send[CORONA] += xReceivedStructure.cars_to_send;
		              for(int i = 0 ; i < xReceivedStructure.cars_to_send ; ++i)
		              {
		            	 // printf("Giving semaphore to CORONA\r\n");
		            	  if(xSemaphoreGive(xCorona_CountingSemaphore) == pdFAIL)
		            	  {

		            	  }
			            	    // printf("FAIL giving semaphore to CORONA\r\n");

		            	 // printf("Gave semaphore to CORONA\r\n");

		              }
					  break;
				}
			}
			else
			{
				/* We did not receive anything from the queue.  This must be an error
			    */
            	xSemaphoreTake( xMutex, portMAX_DELAY );
				printf( "Could not receive from the queue.\r\n" );
				 xSemaphoreGive( xMutex);

				/* An event/message was not received within the expected time. Check for, and if
								 necessary clear, any error conditions that might be
								 preventing the receiving of messages to queue


								 . */
								 //ClearErrors();
			}
		}
}



//Timer ISR
static void randomTimerCallback( TimerHandle_t xTimer )
{
	/* The xHigherPriorityTaskWoken parameter must be initialized to pdFALSE as
	it will get set to pdTRUE inside the interrupt safe API function (xTimerChangePeriodFromISR) if a
	context switch is required.*/
	//The timer service/daemon task spends most of its time in the Blocked state, waiting for messages to arrive on the timer command queue. Calling xTimerChangePeriodFromISR() writes a message to the timer command queue, so has the potential to transition the timer service/ daemon task out of the Blocked state. If calling xTimerChangePeriodFromISR() causes the timer service/daemon task to leave the Blocked state, and the timer service/daemon task has a priority equal to or greater than the currently executing task (the task that was interrupted), then *pxHigherPriorityTaskWoken will get set to pdTRUE internally within the xTimerChangePeriodFromISR() function. If xTimerChangePeriodFromISR() sets this value to pdTRUE, then a context switch should be performed before the interrupt exits.
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	TickType_t xTimeNow;
	const TickType_t xNextRandomTimer = pdMS_TO_TICKS( rand() % 5000 + 50UL );//Timer between 50ms to 5 seconds

	xTimeNow = xTaskGetTickCount();

	printf("TIMER Interrupt START\r\n");

	//As this is an interrupt service routine, only FreeRTOS API functions that end in "FromISR" can be used
	if(xTimerChangePeriodFromISR(xAutoReloadTimer, xNextRandomTimer, &xHigherPriorityTaskWoken) != pdPASS)
	{
		printf("The command to change the timers period was not executed successfully. Previous period is used.\r\n");
	}

	/* 'Give' the semaphore to unblock the delegated send task. "FromISR" insures the task taking the semaphore will only unblock when this ISR exits*/
	xSemaphoreGiveFromISR( xSendBinarySemaphore, &xHigherPriorityTaskWoken );

	//No need for semaphore protection since it is an atomic operation
	++interrupt_count; //used for solving the problem of high rate interrupts getting missed by the delegated task. without this counter the delegated task may miss the 3rd semaphore-give operation in case of high rate interrupts


	printf("randomTimerCallback:  currenTick = %lu  |  xNextRandomTimer = %lu\r\n", xTimeNow, xNextRandomTimer);
	printf("TIMER Interrupt END\r\n");

	/* Pass the xHigherPriorityTaskWoken value into portYIELD_FROM_ISR().  If
	xHigherPriorityTaskWoken was set to pdTRUE inside xTimerChangePeriodFromISR()
	then calling portYIELD_FROM_ISR() will request a context switch.  If
	xHigherPriorityTaskWoken is still pdFALSE then calling
	portYIELD_FROM_ISR() will have no effect.*/
	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );


}


static void vPoliceTask( void *pvParameters )
{

	const TickType_t xMaxExpectedBlockTime = pdMS_TO_TICKS(7000 );//TODO: configuration
	BaseType_t ProceedToNextMission=pdFALSE;

	xSemaphoreTake( xMutex, portMAX_DELAY );
	printf("POLICE task starts!\r\n");
	  xSemaphoreGive( xMutex);


	 for(;;)
	  {
		 xSemaphoreTake( xPolice_CountingSemaphore, portMAX_DELAY);
		 ProceedToNextMission = pdFALSE;

		 while(service_cars_to_send[POLICE])
		 {
			 if(ProceedToNextMission)
			 {
             	 xSemaphoreTake( xMutex, portMAX_DELAY );
				 printf("%s proceeds consecutively to next mission\r\n", (char*)pvParameters);
 				 xSemaphoreGive( xMutex);
			 }
			 service_cars_to_send[POLICE] -=1;
			 //Time on mission:
			 //write to led
         	 xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission START\r\n", (char*)pvParameters);
		     xSemaphoreGive( xMutex);

		     //osDelay(rand()%5000UL+50UL);
            xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission END\r\n", (char*)pvParameters);
			 xSemaphoreGive( xMutex);

			 ProceedToNextMission = pdTRUE;
		 }
		 //set off led
	     xSemaphoreGive(xPolice_CountingSemaphore);
	     //Fixing time in the Garage - I added this delay after giving the semaphore in order to give meaning to creating number of tasks bigger than max concurrent tasks permitted
	    // osDelay(rand()%5000UL+50UL);
	  }
}

static void vFireSquadTask( void *pvParameters )
{
	const TickType_t xMaxExpectedBlockTime = pdMS_TO_TICKS(7000 );//TODO: configuration
	BaseType_t ProceedToNextMission=pdFALSE;

	xSemaphoreTake( xMutex, portMAX_DELAY );
	printf("FIRE SQUADE task starts!\r\n");
	xSemaphoreGive( xMutex);


	 for(;;)
	  {
		 xSemaphoreTake( xFire_CountingSemaphore, portMAX_DELAY);
		 ProceedToNextMission = pdFALSE;

		 while(service_cars_to_send[FIRE_SQUAD])
		 {
			 if(ProceedToNextMission)
			 {
             	xSemaphoreTake( xMutex, portMAX_DELAY );
				 printf("%s proceeds consecutively to next mission\r\n", (char*)pvParameters);
 				 xSemaphoreGive( xMutex);
			 }
			 service_cars_to_send[FIRE_SQUAD] -=1;
			 //Time on mission:
			 //write to led
         	 xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission START\r\n", (char*)pvParameters);
		    xSemaphoreGive( xMutex);

		     //osDelay(rand()%5000UL+50UL);
         	xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission END\r\n", (char*)pvParameters);
			 xSemaphoreGive( xMutex);

			 ProceedToNextMission = pdTRUE;
		 }
		 //set off led
	     xSemaphoreGive(xFire_CountingSemaphore);
	     //Fixing time in the Garage - I added this delay after giving the semaphore in order to give meaning to creating number of tasks bigger than max concurrent tasks permitted
	    // osDelay(rand()%5000UL+50UL);
	  }
}

static void vAmbulanceTask( void *pvParameters )
{
	const TickType_t xMaxExpectedBlockTime = pdMS_TO_TICKS(7000 );//TODO: configuration

	BaseType_t ProceedToNextMission=pdFALSE;

	xSemaphoreTake( xMutex, portMAX_DELAY );
	printf("AMBULANCE task starts!\r\n");
	 xSemaphoreGive( xMutex);


	 for(;;)
	  {
		 xSemaphoreTake( xAmbulance_CountingSemaphore, portMAX_DELAY);
		 ProceedToNextMission = pdFALSE;

		 while(service_cars_to_send[AMBULANCE])
		 {
			 if(ProceedToNextMission)
			 {
             	xSemaphoreTake( xMutex, portMAX_DELAY );
				 printf("%s proceeds consecutively to next mission\r\n", (char*)pvParameters);
 				  xSemaphoreGive( xMutex);

			 }
			 service_cars_to_send[AMBULANCE] -=1;
			 //Time on mission:
			 //write to led
         	xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission START\r\n", (char*)pvParameters);
			 xSemaphoreGive( xMutex);

		    // osDelay(rand()%5000UL+50UL);
         	xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission END\r\n", (char*)pvParameters);
			 xSemaphoreGive( xMutex);

			 ProceedToNextMission = pdTRUE;
		 }
		 //set off led
	     xSemaphoreGive(xAmbulance_CountingSemaphore);
	     //Fixing time in the Garage - I added this delay after giving the semaphore in order to give meaning to creating number of tasks bigger than max concurrent tasks permitted
	    // osDelay(rand()%5000UL+50UL);
	  }

}

static void vCoronaTask( void *pvParameters )
{
	const TickType_t xMaxExpectedBlockTime = pdMS_TO_TICKS(7000 );//TODO: configuration
	BaseType_t ProceedToNextMission=pdFALSE;

	xSemaphoreTake( xMutex, portMAX_DELAY );
	printf("CORONA task starts!\r\n");
	  xSemaphoreGive( xMutex);


	 for(;;)
	  {
     	//xSemaphoreTake( xMutex, portMAX_DELAY );
		 //printf("corona task");
		 xSemaphoreTake( xCorona_CountingSemaphore, portMAX_DELAY);

     	xSemaphoreTake( xMutex, portMAX_DELAY );
		 printf("corona task TOOK Semaphore\r\n");
			  xSemaphoreGive( xMutex);


		 ProceedToNextMission = pdFALSE;

		 while(service_cars_to_send[CORONA])
		 {
			 if(ProceedToNextMission)
			 {
             	xSemaphoreTake( xMutex, portMAX_DELAY );
				 printf("%s proceeds consecutively to next mission\r\n", (char*)pvParameters);
 				  xSemaphoreGive( xMutex);

			 }
			 service_cars_to_send[CORONA] -=1;
			 //Time on mission:
			 //write to led
         	xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission START\r\n", (char*)pvParameters);
				  xSemaphoreGive( xMutex);

		     //osDelay(rand()%5000UL+50UL);
         	xSemaphoreTake( xMutex, portMAX_DELAY );
			 printf("%s mission END\r\n", (char*)pvParameters);
				  xSemaphoreGive( xMutex);

			 ProceedToNextMission = pdTRUE;
		 }
		 //set off led
	     xSemaphoreGive(xCorona_CountingSemaphore);
	     //Fixing time in the Garage - I added this delay after giving the semaphore in order to give meaning to creating number of tasks bigger than max concurrent tasks permitted
	     //osDelay(rand()%5000UL+50UL);
	  }
}




//################################################################
/* USER CODE END 4 */




/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

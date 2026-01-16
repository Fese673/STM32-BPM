/**
  ******************************************************************************
  * File Name          : main.c
  * Description        : Monitor tętna + zegar (2 strony: enkoder KY-040)
  *                      Strona 0: MAX30102 (tętno, SpO2, wykres)
  *                      Strona 1: Zegar (czas z ESP32 po UART2)
  ******************************************************************************
  */


/* Includes ------------------------------------------------------------------*/
/* PODSTAWOWE NAGŁÓWKI SYSTEMOWE */

#include "main.h"                // Deklaracje wygenerowane przez CubeMX (GPIO, piny, prototypy)
#include "stm32f1xx_hal.h"       // Biblioteka HAL dla STM32F1 – warstwa abstrakcji sprzętu
                                 // (timery, GPIO, I2C, UART itd.)


                    
/* USER CODE BEGIN Includes */
/* NAGŁÓWKI DODANE PRZEZ UŻYTKOWNIKA 
 *
 * Są to moduły (sterowniki/biblioteki), które udostępniają funkcje
 * do obsługi poszczególnych urządzeń peryferyjnych.
 *
 * Każdy moduł można traktować jak „osobny main.c” dla danego urządzenia,
 * co pozwala zachować porządek i nie przeładowywać głównego main.c.
 */

#include "oled.h"        // Wyświetlacz OLED SSD1306 (I2C1: PB6=SCL, PB7=SDA)
#include "max30102.h"    // Czujnik tętna MAX30102   (I2C2: PB10=SCL, PB11=SDA)
#include "KY040.h"       // Enkoder obrotowy KY-040  (CLK=PA6, DT=PA4)
#include <stdio.h>       // Standardowe funkcje C (printf, sprintf itd.)                   
                         // (często używane z UART-em oraz do formatowania tekstu)

/* USER CODE END Includes */



/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef  hi2c1;    // I2C1 dla OLED
I2C_HandleTypeDef  hi2c2;    // I2C2 dla MAX30102
TIM_HandleTypeDef  htim3;    // TIM3: PWM 2kHz dla buzzera na PB0 (CH3)
UART_HandleTypeDef huart1;   // UART1 PA9/PA10 (debug PC)
UART_HandleTypeDef huart2;   // UART2 PA2/PA3 (link ESP32)

/* USER CODE BEGIN PV */
// === ENKODER KY-040 ===
KY040_Handle_t encoder;
volatile uint8_t current_page = 0;   // 0=tętno, 1=zegar
volatile uint8_t page_changed = 0;   // flaga zmiany strony

// === ZEGAR (czas z ESP32) ===
volatile uint8_t curHour    = 12;
volatile uint8_t curMinute  = 0;
volatile uint8_t time_valid = 0;     // 0=czekam, 1=czas odebrany (np. z ESP32)

//  Flaga wysyłania BPM (tylko raz na pik)
static uint8_t last_diff_state = 0;  // Pamiętaj poprzedni stan diff

/* USER CODE END PV */


/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_NVIC_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);


/* USER CODE BEGIN 0 */

// UART1 – wysyłanie BPM + echo debug
static uint8_t rxBuffer[100];
static uint8_t rxIndex = 0;

// Nowe: wysyła BPM + SPO2
void uart1_send_vitals(uint8_t bpm, uint8_t spo2) {
    static char tx_buf[32];
    int len = snprintf(tx_buf, sizeof(tx_buf), "BPM:%d,SPO2:%d\r\n", bpm, spo2);
    HAL_UART_Transmit(&huart1, (uint8_t *)tx_buf, (uint16_t)len, 100);
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        uint8_t receivedByte = rxBuffer[rxIndex];

        // Echo na UART1
        HAL_UART_Transmit(&huart1, &receivedByte, 1, 100);

        if (receivedByte == '\n') {
            rxIndex = 0;
        } else {
            rxIndex++;
            if (rxIndex >= sizeof(rxBuffer)) rxIndex = 0;
        }

        // Ponów odbiór następnego bajtu
        HAL_UART_Receive_IT(&huart1, &rxBuffer[rxIndex], 1);
    }
}

/* USER CODE END 0 */



/**
  * @brief  Funkcja główna
  */
int main(void)
{
    /* MCU Configuration --------------------------------------------------------*/
    HAL_Init();
    SystemClock_Config(); // 72 MHz

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_I2C2_Init();
    MX_TIM3_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_NVIC_Init();

    /* USER CODE BEGIN 2 */

    HAL_Delay(200); // stabilizacja zasilania

    // Inicjalizacja peryferiów użytkownika
    OLED_Init();       // ekran
    max30102_init();   // czujnik tętna

    // Enkoder: CLK=PA6, DT=PA4
    KY040_Init(&encoder, GPIOA, GPIO_PIN_6, GPIOA, GPIO_PIN_4);

    // UART1 – start odbioru w trybie przerwań (1 bajt)
    HAL_UART_Receive_IT(&huart1, &rxBuffer[0], 1);

    // Buzzer: start PWM na TIM3_CH3 (PB0)
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);

    /* USER CODE END 2 */

    /* Infinite loop */
    while (1)
    {
        /* ========== BLOK 1: ENKODER - PRZEŁĄCZANIE STRON ========== */
        KY040_Direction_t dir = KY040_GetRotation(&encoder);

        if (dir == KY040_CLOCKWISE)
        {
            current_page++;
            if (current_page > 1) current_page = 0;
            page_changed = 1;
        }
        else if (dir == KY040_COUNTERCLOCKWISE)
        {
            if (current_page == 0)
                current_page = 1;
            else
                current_page--;
            page_changed = 1;
        }

        /* ========== BLOK 2: ODŚWIEŻENIE EKRANU ========== */
        if (page_changed)
        {
            page_changed = 0;

            if (current_page == 0)
            {
                // STRONA 0: MAX30102 (przywróć układ początkowy)
                OLED_Init();
            }
            else
            {
                // STRONA 1: ZEGAR (czas z ESP32)
                OLED_Clear();

                uint8_t h10 = curHour / 10;
                uint8_t h1  = curHour % 10;
                uint8_t m10 = curMinute / 10;
                uint8_t m1  = curMinute % 10;

                OLED_showNumAt(37, h10);
                OLED_showNumAt(51, h1);
                OLED_ShowString(65, 4, ":");
                OLED_showNumAt(75, m10);
                OLED_showNumAt(89, m1);

                // Status synchronizacji czasu
                if (!time_valid) {
                    OLED_ShowString(10, 0, "Czekam TIME...");
                } else {
                    OLED_ShowString(10, 0, "TIME OK");
                }
            }
        }

        /* ========== BLOK 3: MAX30102 - POMIAR TĘTNA ========== */
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET)
        {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);  // LED diagnostyczny
            max30102_cal();

            uint8_t spo2      = max30102_getSpO2();
            uint8_t heartReat = max30102_getHeartRate();
            int16_t diff      = max30102_getDiff();

            

            // Wyświetlanie tylko na stronie 0
            if (current_page == 0)
            {
                // Tętno (3 cyfry)
                if (heartReat % 1000 / 100 > 0)
                    OLED_showNum(0, heartReat % 1000 / 100);
                else
                    OLED_showNum(0, 10);  // spacja

                OLED_showNum(1, heartReat % 100 / 10);
                OLED_showNum(2, heartReat % 10);

                // SpO2 (3 cyfry)
                if (spo2 % 1000 / 100 > 0)
                    OLED_showNum(3, spo2 % 1000 / 100);
                else
                    OLED_showNum(3, 10);  // spacja

                OLED_showNum(4, spo2 % 100 / 10);
                OLED_showNum(5, spo2 % 10);

                // Wykres PPG
                OLED_drawChart(diff);

                // Ikona serca
                if (diff > 50)
                    OLED_showHeart(0);  // duże
                else
                    OLED_showHeart(1);  // małe
            }

            // Buzzer + oscyloskop (zawsze, niezależnie od strony) + wysyłanie przez uart 
            if (diff > 50)
            {
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 250);  // 50% duty

                  // WYSYŁAJ TYLKO GDY PULSUJE PO RAZ PIERWSZY
                if (last_diff_state == 0)
                {
                    uart1_send_vitals(heartReat, spo2);  //  ( obie wartości)
                    last_diff_state = 1;
                }

            }
            else
            {
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);

                last_diff_state = 0;  //  RESET FLAGI - gotowy na następny pik
            }
        }
    }
}


/* ============================================================================ */
/* KONFIGURACJA PERYFERIÓW                                                     */
/* ============================================================================ */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

static void MX_NVIC_Init(void)
{
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);

    // UART1 – debug
    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    // UART2 – ESP32
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    // EXTI4 (PA4 - enkoder DT)
    HAL_NVIC_SetPriority(EXTI4_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);

    // EXTI9_5 (PA6 - enkoder CLK)
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

static void MX_TIM3_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    // PB0 = TIM3_CH3 (no remap)
    GPIO_InitStruct.Pin   = GPIO_PIN_0;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler         = 71;   // 72MHz/72 = 1MHz
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 499;  // 1MHz/500 = 2kHz
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void MX_USART1_UART_Init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    huart1.Instance        = USART1;
    huart1.Init.BaudRate   = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits   = UART_STOPBITS_1;
    huart1.Init.Parity     = UART_PARITY_NONE;
    huart1.Init.Mode       = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void MX_USART2_UART_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    huart2.Instance        = USART2;
    huart2.Init.BaudRate   = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits   = UART_STOPBITS_1;
    huart2.Init.Parity     = UART_PARITY_NONE;
    huart2.Init.Mode       = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;                    // Wskazanie dokładne sprzętu na I2C1 (0x40005400)
    hi2c1.Init.ClockSpeed      = 400000;                  // Ustawienie prędkości 400kHz w Cube MX
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_16_9;      // SCL high : low = 16 : 9
    hi2c1.Init.OwnAddress1     = 0;                       // Własny adres urządzenia (master) adres niepotrzebny dla mastera (0)        
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT; // Tryb adresowania ustaiwany zawsze tak samo
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE; // Wyłączenie drugiego adresu (bez reakcji)
    hi2c1.Init.OwnAddress2     = 0;                       // Nieistotne , 0 dla formalności
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE; // MCU ignoruje adres 0x00 (Adres przeznaczony np do resetu wszystkich slave)
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;   // clock stretching DOZWOLONY

    /*WYWOŁANIE KONFIGURACJI SPRZĘTU*/
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)     // Robi wszystko co skonfirowaliśmy powyżej w formie bitów w rejestrze
    {
        _Error_Handler(__FILE__, __LINE__); // Jeśli: zegar nie działa, ma złe parametry ,timeout | HAL zwraca HAL_ERROR
    }
}

static void MX_I2C2_Init(void)
{
    hi2c2.Instance             = I2C2;
    hi2c2.Init.ClockSpeed      = 400000;
    hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_16_9;
    hi2c2.Init.OwnAddress1     = 0;
    hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2     = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c2) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void MX_GPIO_Init(void) // Konfiguracja wszystkich pinów GPIO
{
    GPIO_InitTypeDef GPIO_InitStruct;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /* Stany początkowe wyjść */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);

    /* PA7 (LED testowy) */
    GPIO_InitStruct.Pin   = GPIO_PIN_7;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PC13 (LED diagnostyczny) */
    GPIO_InitStruct.Pin   = GPIO_PIN_13;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PA1 (Oscyloskop - marker) */
    GPIO_InitStruct.Pin   = GPIO_PIN_1;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB12 (MAX30102 INT) */
    GPIO_InitStruct.Pin  = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Enkoder: PA6 (CLK), PA4 (DT) */
    GPIO_InitStruct.Pin  = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1: PA9 (TX), PA10 (RX) */
    GPIO_InitStruct.Pin   = GPIO_PIN_9;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART2: PA2 (TX), PA3 (RX) */
    GPIO_InitStruct.Pin   = GPIO_PIN_2;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
  * @brief  Obsługa błędów
  */
void _Error_Handler(char *file, int line)
{
    while (1)
    {
        // Jakis kiedys led diagonstyczny lub inna komenda 
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif

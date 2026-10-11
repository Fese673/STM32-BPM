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

#include "oled/oled.h"                         // Wyswietlacz OLED SSD1306 (I2C1: PB6=SCL, PB7=SDA)
#include "max30102/max30102.h"                 // Czujnik tetna MAX30102   (I2C2: PB10=SCL, PB11=SDA)
#include <stdio.h>       // Standardowe funkcje C (printf, sprintf itd.)
#include "comm/uart1.h"
#include "comm/uart2.h"
#include "comm/frame_protocol.h"
#include "radar/ld2410_radar_wrapper.h"
#include "pwm/pwm_reg.h"
#include "comm/brightness_handler.h"
#include "stepper/stepper_link.h"

/* USER CODE END Includes */



/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef  hi2c1;    // I2C1 dla OLED
I2C_HandleTypeDef  hi2c2;    // I2C2 dla MAX30102
TIM_HandleTypeDef  htim3;    // TIM3: buzzer PB0 (CH3) + jasnosc 7-seg PA6 (CH1)
UART_HandleTypeDef huart1;   // UART1 PA9/PA10 -> ZEGAR ESP32 (ramki COBS, 9600)
UART_HandleTypeDef huart2;   // UART2 PA2/PA3 -> radar LD2410 (256000)

/* USER CODE BEGIN PV */
//  Flaga wysylania BPM (tylko raz na pik)
static uint8_t last_diff_state = 0;  // Pamietaj poprzedni stan diff

/* USER CODE END PV */


/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_NVIC_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
void MX_USART2_UART_Init(void);
static void i2c_ctrl_poll(void);


/* USER CODE BEGIN 0 */
// USART1 (PA9/PA10) @ 9600: kanal ramek COBS+CRC16 <-> ZEGAR ESP32
// USART2 (PA2/PA3) @ 256000: radar LD2410
// CH340 niepodłączony — PA9 idzie bezpośrednio do ZEGAR GPIO16.
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
    PWM_Reg_Init();   // start TIM3_CH1 (PA6) — jasnosc 7-seg, domyslnie 0
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_NVIC_Init();

    /* USER CODE BEGIN 2 */

    HAL_Delay(200); // stabilizacja zasilania

    // UART1 — kanal ramek COBS do ZEGAR ESP32 (PA9/PA10, 9600 baud)
    uart1_init();
    frame_protocol_init();
    brightness_handler_init();   // ramka 0xB0 -> PWM_Reg_SetDuty
    stepper_init();       // ramka 0xB1 -> ring steppera
    uart1_enable_rx();

    // Radar LD2410 na USART2 (PA2/PA3, 256000 baud)
    ld2410_radar_init();

    oled_zombie     = OLED_Detect()     ? 0 : 1;
    max30102_zombie = max30102_Detect() ? 0 : 1;

    if (!oled_zombie) {
        OLED_Init();       // ekran (non-blocking IT chain)
    }

    if (!max30102_zombie) {
        max30102_init();   // czujnik tetna
    }

    // Buzzer: start PWM na TIM3_CH3 (PB0)
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);

    /* USER CODE END 2 */

    /* Superloop. Dioda PC13 miga co 100 ms Z PETLI — jej stanie
     * znaczy, ze petla stoi. Nic innego diody nie rusza. */
    uint32_t last_i2c_ms = 0U;
    uint32_t last_led_ms = 0U;
    while (1)
    {
        frame_rx_process();          // ramki COBS z ZEGAR (nieblokujace)

        if ((HAL_GetTick() - last_led_ms) >= 100U) {
            last_led_ms = HAL_GetTick();
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }

        if ((HAL_GetTick() - last_i2c_ms) >= 20U) {
            last_i2c_ms = HAL_GetTick();
            i2c_ctrl_poll();         // OLED + MAX30102 co 20 ms
        }

        ld2410_radar_poll();         // radar (wewnetrzny throttle 50/500 ms)
        stepper_poll();              // silnik (wewnetrzna bramka 1 ms)
    }
}

/* Poll czujnikow co 20 ms (odpowiednik dawnego i2c_ctrl_task).
 * OLED non-blocking: tylko wpisy do cache + watchdog/kick/recover. */
static void i2c_ctrl_poll(void)
{
    OLED_WatchdogPoll();
    if (OLED_GetError() && !oled_zombie) {
        OLED_Recover();
    } else if (!oled_zombie) {
        OLED_Kick();
    }

    if (max30102_zombie)
    {
        /* Fallback bez czujnika: ekran ma zyc — myslniki, plaski
         * przewijany wykres i napis. */
        uint8_t d;
        for (d = 0; d < 6; d++) OLED_showNum(d, 10);
        OLED_drawChart(0.0f);
        OLED_showHeart(1);
        OLED_ShowString(0, 0, "BRAK CZUJNIKA ");
    }
    else
    {
        max30102_cal();
        (void)max30102_getStatus();

        uint8_t spo2      = max30102_getSpO2();
        uint8_t heartReat = max30102_getHeartRate();
        int16_t diff      = max30102_getDiff();

        if (heartReat > 0)
        {
            if (heartReat >= 100)
                OLED_showNum(0, heartReat / 100);
            else
                OLED_showNum(0, 10);
            OLED_showNum(1, (heartReat % 100) / 10);
            OLED_showNum(2, heartReat % 10);
        }
        else
        {
            OLED_showNum(0, 10);
            OLED_showNum(1, 0);
            OLED_showNum(2, 0);
        }

        if (spo2 > 0)
        {
            if (spo2 >= 100)
                OLED_showNum(3, 1);
            else
                OLED_showNum(3, 10);
            OLED_showNum(4, (spo2 % 100) / 10);
            OLED_showNum(5, spo2 % 10);
        }
        else
        {
            OLED_showNum(3, 10);
            OLED_showNum(4, 0);
            OLED_showNum(5, 0);
        }

        OLED_drawChart(diff);

        if (diff > 50)
            OLED_showHeart(0);
        else
            OLED_showHeart(1);

        if (diff > 50) {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 250);

            if (last_diff_state == 0) {
                (void)frame_tx_bpm(heartReat, spo2);
                last_diff_state = 1;
            }
        } else {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
            last_diff_state = 0;
        }

        (void)frame_tx_ppg(diff);
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

    // SysTick — bazowy tick HAL (najwyzszy priorytet)
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

    // I2C1 (OLED, lancuch IT + recovery z DeInit)
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);

    // UART1 — kanal ramek COBS do ZEGAR
    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    // UART2 — radar LD2410
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
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

    // PB0 = TIM3_CH3 (buzzer, no remap)
    GPIO_InitStruct.Pin   = GPIO_PIN_0;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PA6 = TIM3_CH1 (jasnosc wyswietlaczy 7-seg, no remap)
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin   = GPIO_PIN_6;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler         = 7;    // 72MHz/8 = 9MHz
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 599;  // 9MHz/600 = 15kHz (wspolny dla CH1 i CH3)
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

    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void MX_USART1_UART_Init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    huart1.Instance        = USART1;
    huart1.Init.BaudRate   = 9600;
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

void MX_USART2_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_2;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_3;
    GPIO_InitStruct.Mode      = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart2.Instance           = USART2;
    huart2.Init.BaudRate      = 256000;
    huart2.Init.WordLength    = UART_WORDLENGTH_8B;
    huart2.Init.StopBits      = UART_STOPBITS_1;
    huart2.Init.Parity        = UART_PARITY_NONE;
    huart2.Init.Mode          = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl     = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling  = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK) {
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

    /* PC13 (LED diagnostyczny — miga z petli co 100 ms) */
    GPIO_InitStruct.Pin   = GPIO_PIN_13;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PA1 (Oscyloskop - marker) */
    GPIO_InitStruct.Pin   = GPIO_PIN_1;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB12 (MAX30102 INT — nieuzywany w pollingu, zostawiony jako wejscie) */
    GPIO_InitStruct.Pin  = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PA4/PA5/PA7/PA8 (silnik krokowy) — konfiguruje stepper_hw_init().
     * PA6 (jasnosc 7-seg, TIM3_CH1) — konfiguruje MX_TIM3_Init(). */

    /* USART1: PA9 (TX), PA10 (RX) */
    GPIO_InitStruct.Pin   = GPIO_PIN_9;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA10 pull-up: RX od ZEGAR GPIO17. Bez pull-up floating → szumy → overrun.
    GPIO_InitStruct.Pin  = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
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

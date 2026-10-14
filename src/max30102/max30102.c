#include "stm32f1xx_hal.h"
#include "max30102/max30102.h"

#define MAX30102_ADDR_WRITE 0xae
#define MAX30102_ADDR_READ  0xaf

#define RES_INTERRUPT_STATUS_1              0x00
#define RES_INTERRUPT_STATUS_2              0x01
#define RES_INTERRUPT_ENABLE_1              0x02
#define RES_INTERRUPT_ENABLE_2              0x03
#define RES_FIFO_WRITE_POINTER              0x04
#define RES_OVERFLOW_COUNTER                0x05
#define RES_FIFO_READ_POINTER               0x06
#define RES_FIFO_DATA_REGISTER              0x07
#define RES_FIFO_CONFIGURATION              0x08
#define RES_MODE_CONFIGURATION              0x09
#define RES_SPO2_CONFIGURATION              0x0a
#define RES_LED_PLUSE_AMPLITUDE_1           0x0c
#define RES_LED_PLUSE_AMPLITUDE_2           0x0d
#define RES_PROXIMITY_MODE_LED_PLUSE_AMPLITUDE 0x10
#define RES_MULTI_LED_MODE_CONTROL_1        0x11
#define RES_MULTI_LED_MODE_CONTROL_2        0x12
#define RES_DIE_TEMP_INTEGER                0x1f
#define RES_DIE_TEMP_FRACTION               0x20
#define RES_DIE_TEMPERATURE_CONFIG          0x21
#define RES_PROXIMITY_INTERRUPT_THRESHOLD   0x30
#define RES_REVISION_ID                     0xfe
#define RES_PART_ID                         0xff

extern I2C_HandleTypeDef hi2c2;

volatile uint8_t max30102_zombie = 0;

/* Dynamiczne zombie: N kolejnych faili I2C w getterach = czujnik chory.
 * Chory czujnik palil timeouty ~40 ms na poll 20 ms i dusil silnik —
 * bez tego licznika petla wisiala tak az do NRST. Po trip: zombie=1
 * (zero ruchu I2C) + re-probe co 2 s w cal() (samouzdrowienie). */
#define MAX_FAIL_TRIP 10U
#define MAX_REPROBE_MS 2000U
static uint8_t s_fail_count = 0U;
static uint32_t s_reprobe_tick = 0U;

static void max_note(HAL_StatusTypeDef st)
{
    if (st == HAL_OK) {
        s_fail_count = 0U;
        return;
    }
    if (s_fail_count < 255U) ++s_fail_count;
    if (s_fail_count >= MAX_FAIL_TRIP) {
        max30102_zombie = 1;
        s_reprobe_tick = HAL_GetTick();
    }
}

/* Odbudowa magistrali I2C2:
 * 1) Slave (MAX30102) moze trzymac SDA nisko po przerwanym transferze —
 *    DeInit/Init mastera tego nie zwolni, trzeba wygenerowac recznie
 *    do 9 impulsow SCL + STOP (open-drain, jak prawdziwy master I2C).
 * 2) Potem DeInit/Init peryferium (F1: flaga BUSY potrafi sie zatrzasnac
 *    i zaden timeout tego nie wyczysci, a NRST tak).
 * Wywolywac przed re-probe. */
static void max_i2c_recover(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Obrona: HAL na niezainicjalizowanym hi2c2 (Instance==NULL) pisalby
     * rejestrami I2C pod adres 0x00000000 (alias wektorow we flashu!). */
    if (hi2c2.Instance == NULL) return;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin   = GPIO_PIN_10 | GPIO_PIN_11;   /* SCL | SDA */
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);   /* SDA w gore */
    for (uint8_t i = 0U; i < 9U; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
        for (volatile uint8_t d = 0U; d < 30U; d++) { __NOP(); }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
        for (volatile uint8_t d = 0U; d < 30U; d++) { __NOP(); }
    }
    /* STOP: SDA 0->1 przy SCL wysokim */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
    for (volatile uint8_t d = 0U; d < 30U; d++) { __NOP(); }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
    for (volatile uint8_t d = 0U; d < 30U; d++) { __NOP(); }

    HAL_I2C_DeInit(&hi2c2);
    HAL_I2C_Init(&hi2c2);
}

uint8_t max30102_Detect(void)
{
    if (hi2c2.Instance == NULL) return 0;   /* I2C2 nie zainicjalizowane */
    if (HAL_I2C_IsDeviceReady(&hi2c2, MAX30102_ADDR_WRITE, 2, 5) == HAL_OK)
        return 1;
    return 0;
}

void max30102_init(void)
{
    if (hi2c2.Instance == NULL) { max30102_zombie = 1; return; }
    if (max30102_zombie) return;

    uint8_t data = 0;
    HAL_StatusTypeDef status;

    /* reset */
    data = 0x40;
    status = HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_MODE_CONFIGURATION,
                               I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
    if (status != HAL_OK) { max30102_zombie = 1; return; }

    uint8_t rst_guard = 0;
    do
    {
        status = HAL_I2C_Mem_Read(&hi2c2, MAX30102_ADDR_READ, RES_MODE_CONFIGURATION,
                                  I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
        if (status != HAL_OK) { max30102_zombie = 1; return; }
        if (++rst_guard > 20) { max30102_zombie = 1; return; }
    } while (data & 0x40);

    /* New Data Interrupt */
    data = 0x40;
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_INTERRUPT_ENABLE_1,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);

    /* 16384-range 50Hz 18-bit ADC resolution */
    data = 0x63;
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_SPO2_CONFIGURATION,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);

    /* Light brightness */
    data = 0x47;   // 0x47 x 0.2 mA ~= 14 mA — bezpieczne i optymalne
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_LED_PLUSE_AMPLITUDE_1,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_LED_PLUSE_AMPLITUDE_2,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE,
                      RES_PROXIMITY_MODE_LED_PLUSE_AMPLITUDE,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);

    /* FIFO clear */
    data = 0;
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_FIFO_WRITE_POINTER,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_OVERFLOW_COUNTER,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_FIFO_READ_POINTER,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);

    /* FIFO rollover ON (bit 4). Bez tego po zapchaniu 32 probek FIFO
     * przestaje sie zapisywac (WR==RD), sterownik widzi 0 probek i juz
     * nigdy nic nie odczyta — BPM/SpO2 umiera do resetu. */
    data = 0x10;
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_FIFO_CONFIGURATION,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);

    /* interrupt status clear */
    max30102_getStatus();

    /* SPO2 Mode */
    data = 0x03;
    HAL_I2C_Mem_Write(&hi2c2, MAX30102_ADDR_WRITE, RES_MODE_CONFIGURATION,
                      I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
}

uint8_t max30102_getUnreadSampleCount(void)
{
    if (max30102_zombie || hi2c2.Instance == NULL) return 0;

    uint8_t wr = 0, rd = 0;

    max_note(HAL_I2C_Mem_Read(&hi2c2, MAX30102_ADDR_READ, RES_FIFO_WRITE_POINTER,
                     I2C_MEMADD_SIZE_8BIT, &wr, 1, 10));
    max_note(HAL_I2C_Mem_Read(&hi2c2, MAX30102_ADDR_READ, RES_FIFO_READ_POINTER,
                     I2C_MEMADD_SIZE_8BIT, &rd, 1, 10));

    if ((wr - rd) < 0)
        return wr - rd + 32;
    else
        return wr - rd;
}

void max30102_getFIFO(SAMPLE *data, uint8_t sampleCount)
{
    if (max30102_zombie || hi2c2.Instance == NULL) return;

    uint8_t dataTemp[5 * 6];
    uint8_t i;

    if (sampleCount > 5)
        sampleCount = 5;

    max_note(HAL_I2C_Mem_Read(&hi2c2, MAX30102_ADDR_READ, RES_FIFO_DATA_REGISTER,
                     I2C_MEMADD_SIZE_8BIT, dataTemp,
                     6 * sampleCount, 25));

    for (i = 0; i < sampleCount; i++)
    {
        data[i].red = (((uint32_t)dataTemp[i * 6])     << 16 |
                       ((uint32_t)dataTemp[i * 6 + 1]) << 8  |
                        (uint32_t)dataTemp[i * 6 + 2]) & 0x3ffff;

        data[i].iRed = (((uint32_t)dataTemp[i * 6 + 3]) << 16 |
                        ((uint32_t)dataTemp[i * 6 + 4]) << 8  |
                         (uint32_t)dataTemp[i * 6 + 5]) & 0x3ffff;
    }
}

uint8_t max30102_getStatus(void)
{
    if (max30102_zombie || hi2c2.Instance == NULL) return 0;

    uint8_t data = 0, dataTemp = 0;

    max_note(HAL_I2C_Mem_Read(&hi2c2, MAX30102_ADDR_READ, RES_INTERRUPT_STATUS_1,
                     I2C_MEMADD_SIZE_8BIT, &dataTemp, 1, 10));
    data = dataTemp;

    max_note(HAL_I2C_Mem_Read(&hi2c2, MAX30102_ADDR_READ, RES_INTERRUPT_STATUS_2,
                     I2C_MEMADD_SIZE_8BIT, &dataTemp, 1, 10));

    return data | dataTemp;
}

#define BUFF_SIZE 50
SAMPLE sampleBuff[BUFF_SIZE];

uint8_t  heartRate = 0;
uint8_t  spo2      = 0;

uint32_t redAC  = 0;
uint32_t redDC  = 0;
uint32_t iRedAC = 0;
uint32_t iRedDC = 0;

#define FILTER_LEVEL 8 /* Filter Level */
void filter(SAMPLE *s)
{
    uint8_t  i;
    uint32_t red  = 0;
    uint32_t ired = 0;

    for (i = 0; i < FILTER_LEVEL - 1; i++)
    {
        red  += sampleBuff[i].red;
        ired += sampleBuff[i].iRed;
    }

    s->red  = (red  + s->red)  / FILTER_LEVEL;
    s->iRed = (ired + s->iRed) / FILTER_LEVEL;
}

void buffInsert(SAMPLE s)
{
    uint8_t i;

    for (i = BUFF_SIZE - 1; i > 0; i--)
    {
        sampleBuff[i].red  = sampleBuff[i - 1].red;
        sampleBuff[i].iRed = sampleBuff[i - 1].iRed;
    }

    sampleBuff[0].red  = s.red;
    sampleBuff[0].iRed = s.iRed;
}

void calAcDc(uint32_t *rac, uint32_t *rdc,
             uint32_t *iac, uint32_t *idc)
{
    uint32_t rMax = sampleBuff[0].red;
    uint32_t rMin = sampleBuff[0].red;
    uint32_t iMax = sampleBuff[0].iRed;
    uint32_t iMin = sampleBuff[0].iRed;

    uint8_t i;
    for (i = 0; i < BUFF_SIZE; i++)
    {
        if (sampleBuff[i].red > rMax)  rMax = sampleBuff[i].red;
        if (sampleBuff[i].red < rMin)  rMin = sampleBuff[i].red;
        if (sampleBuff[i].iRed > iMax) iMax = sampleBuff[i].iRed;
        if (sampleBuff[i].iRed < iMin) iMin = sampleBuff[i].iRed;
    }

    *rac = rMax - rMin;
    *rdc = (rMax + rMin) / 2;
    *iac = iMax - iMin;
    *idc = (iMax + iMin) / 2;
}

int16_t eachSampleDiff = 0; // How different is it from the previous sample?

void max30102_cal(void)
{
    /* Twarda blokada: bez zainicjalizowanego I2C2 nie ruszamy HAL-a
     * (kazde HAL_I2C_* na Instance==NULL to UB — patrz v2.2 freeze). */
    if (hi2c2.Instance == NULL) return;

    if (max30102_zombie) {
        /* Re-probe co 2 s: najpierw odbudowa I2C2 (BUSY-lockup), potem
         * jeden IsDeviceReady (do 10 ms), bez wiszenia. Jak czujnik
         * wrocil: pelny re-init i jazda dalej, bez NRST. */
        uint32_t now = HAL_GetTick();
        if ((now - s_reprobe_tick) >= MAX_REPROBE_MS) {
            s_reprobe_tick = now;
            max_i2c_recover();
            if (max30102_Detect()) {
                max30102_zombie = 0;
                s_fail_count = 0U;
                max30102_init();
            }
        }
        return;
    }

    uint8_t unreadSampleCount = max30102_getUnreadSampleCount();
    /* Clamp do rozmiaru sampleBuffTemp (5 slotów): FIFO trzyma do 31
     * nieprzeczytanych próbek (backlog po dotknięciu palcem / jitterze),
     * a pętla niżej czyta i filtruje sampleBuffTemp[i] — bez tego OOB
     * read + OOB write przez filter() rozwalał stos taska i wieszał OLED. */
    if (unreadSampleCount > 5) unreadSampleCount = 5;
    SAMPLE  sampleBuffTemp[5];
    uint8_t i, ii;

    max30102_getFIFO(sampleBuffTemp, unreadSampleCount);

    static uint8_t  eachBeatSampleCount      = 0;   // How many samples did this heartbeat endure?
    static uint8_t  lastTenBeatSampleCount[10];     // Number of samples for each of the past ten heartbeats
    static uint8_t  beatsCollected           = 0;   // How many beats are currently in the buffer
    static uint32_t last_iRed                = 0;   // The previous infrared value, filtered

    for (i = 0; i < unreadSampleCount; i++)
    {
        if (sampleBuffTemp[i].iRed < 40000) // No fingers, no counting; skip it.
        {
            heartRate      = 0;
            spo2           = 0;
            eachSampleDiff = 0;
            eachBeatSampleCount = 0;
            beatsCollected = 0;
            for (ii = 0; ii < 10; ii++)
                lastTenBeatSampleCount[ii] = 0;
            continue;
        }

        buffInsert(sampleBuffTemp[i]);
        calAcDc(&redAC, &redDC, &iRedAC, &iRedDC);
        filter(&sampleBuffTemp[i]);

        // Calculation spo2
        float R = (((float)redAC  / (float)redDC) /
                   ((float)iRedAC / (float)iRedDC));

        if (R >= 0.36f && R < 0.66f)
            spo2 = (uint8_t)(107.0f - 20.0f * R);
        else if (R >= 0.66f && R < 1.0f)
            spo2 = (uint8_t)(129.64f - 54.0f * R);

        // Calculate heart rate, 30–250 ppm, count: 200–12
        eachSampleDiff = (int16_t)(last_iRed - sampleBuffTemp[i].iRed);

        if (eachSampleDiff > 50 && eachBeatSampleCount > 12)
        {
            for (ii = 9; ii > 0; ii--)
                lastTenBeatSampleCount[ii] = lastTenBeatSampleCount[ii - 1];

            lastTenBeatSampleCount[0] = eachBeatSampleCount;

            if (beatsCollected < 255U) ++beatsCollected;

            /* Pierwszy wpis to odcinek od wlozenia palca, nie pelny
             * interwal RR — usredniamy dopiero od 2. uderzenia. */
            uint8_t validBeats = (beatsCollected > 10U)
                                 ? 10U : (uint8_t)(beatsCollected - 1U);

            if (validBeats > 0U)
            {
                uint32_t totalTime = 0;
                for (ii = 0; ii < validBeats; ii++)
                    totalTime += lastTenBeatSampleCount[ii];

                heartRate = (uint8_t)(60.0f * (float)validBeats / 0.02f / (float)totalTime);
            }

            eachBeatSampleCount = 0;
        }

        last_iRed = sampleBuffTemp[i].iRed;
        eachBeatSampleCount++;
    }
}

uint8_t  max30102_getHeartRate(void) { return heartRate; }
uint8_t  max30102_getSpO2(void)      { return spo2; }
int16_t  max30102_getDiff(void)      { return eachSampleDiff; }

#include "stm32f1xx_hal.h"
#include "oled.h"
#include "oledfont.h"

#define SIZE 16
#define XLevelL 0x02
#define XLevelH 0x10
#define Max_Column 128
#define Max_Row 64
#define Brightness 0xFF
#define X_WIDTH 128
#define Y_WIDTH 64

#define OLED_NUM_A 0x1
#define OLED_NUM_B 0x2
#define OLED_NUM_C 0x4
#define OLED_NUM_D 0x8
#define OLED_NUM_E 0x10
#define OLED_NUM_F 0x20
#define OLED_NUM_G 0x40

#define OLED_I2C_TIMEOUT_MS 10      /* 1 B @400 kHz to ~30 us; 10 ms to spory zapas */
#define OLED_I2C_MAX_RETRIES 3
#define OLED_IT_WAIT_TIMEOUT_MS 10  /* limit spina na koniec transferu IT */
#define OLED_DET_TRIALS_BOOT 2U     /* sondowanie dokładne (start) */
#define OLED_DET_TIMEOUT_BOOT_MS 5U
#define OLED_DET_TRIALS_TASK 1U     /* sondowanie lekkie (task, recovery) */
#define OLED_DET_TIMEOUT_TASK_MS 3U
#define OLED_REC_PROBE_EVERY_N 10U  /* pełne sondowanie co N cykli taska */

extern I2C_HandleTypeDef hi2c1;

/* Aktywny adres OLED na magistrali I2C1 (8-bit dla HAL). Ustawia go
 * OLED_Detect() — patrz oled_addr_candidates[]. */
volatile uint8_t oled_addr = 0x78U;

/* Kandydaci na adres SSD1306: 0x3C (SA0=LOW), 0x3D (SA0=HIGH).
 * Pierwszy, który odpowie ACK, trafia do oled_addr. */
const uint8_t oled_addr_candidates[OLED_ADDR_CANDIDATES_COUNT] = {
    0x78U,   /* 0x3C << 1 */
    0x7AU    /* 0x3D << 1 */
};

uint8_t point_x = 0;
uint8_t point_y = 0;
uint8_t oled_cache[8][128];

volatile uint8_t oled_i2c_error = 0;
volatile uint8_t oled_zombie = 0;
static uint8_t whatToDo = 0;
static volatile uint8_t oled_it_active = 0;
static uint8_t display_off_step = 0;

/* Nieblokujący recovery: maszyna stanów napędzana kolejnymi wywołaniami
 * OLED_Recover() z taska (co ~20 ms). Zamiast HAL_Delay czas odmierzamy
 * znacznikami HAL_GetTick(). Flaga oled_i2c_error pozostaje ustawiona
 * przez cały recovery, żeby task wołał nas w każdym cyklu; czyści ją
 * dopiero pomyślny OLED_Init() na końcu. */
typedef enum {
    OLED_REC_IDLE = 0,
    OLED_REC_WAIT_AFTER_DEINIT,  /* odczekaj 10 ms po DeInit */
    OLED_REC_WAIT_AFTER_INIT     /* odczekaj 50 ms po Init */
} oled_rec_state_t;

static oled_rec_state_t oled_rec_state = OLED_REC_IDLE;
static uint32_t oled_rec_tick = 0U;
static uint8_t oled_rec_probe_div = 0U;

/* Znacznik postępu łańcucha odświeżania (ostatni ukończony transfer IT).
 * Pisany w ISR, czytany przez OLED_WatchdogPoll() w tasku. */
static volatile uint32_t oled_chain_tick = 0U;
static volatile uint32_t oled_it_started_tick = 0U;
#define OLED_CHAIN_STALL_MS 200U  /* brak transferu dłużej = łańcuch stoi */

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C1)
        return;

    oled_it_active = 0;
    oled_chain_tick = HAL_GetTick();  /* postęp dla watchdoga */
    oled_it_started_tick = oled_chain_tick;

    static uint8_t y = 0;
    static uint8_t dat = 0;
    HAL_StatusTypeDef status;

    if (whatToDo == 4)
    {
        static const uint8_t display_off_cmds[] = {0x8D, 0x10, 0xAE};
        if (display_off_step < 3)
        {
            dat = display_off_cmds[display_off_step];
            status = HAL_I2C_Mem_Write_IT(&hi2c1, oled_addr, 0x00, I2C_MEMADD_SIZE_8BIT, &dat, 1);
            if (status == HAL_OK)
            {
                oled_it_active = 1;
                display_off_step++;
            }
            else
            {
                oled_i2c_error = 1;
                whatToDo = 0;
                display_off_step = 0;
            }
        }
        else
        {
            display_off_step = 0;
            whatToDo = 0;
        }
    }
    else if (whatToDo == 3)
    {
        dat = 0x00;
        status = HAL_I2C_Mem_Write_IT(&hi2c1, oled_addr, 0x00, I2C_MEMADD_SIZE_8BIT, &dat, 1);
        if (status == HAL_OK)
        {
            oled_it_active = 1;
            whatToDo = 0;
        }
        else
        {
            oled_i2c_error = 1;
        }
    }
    else if (whatToDo == 2)
    {
        dat = 0x10;
        status = HAL_I2C_Mem_Write_IT(&hi2c1, oled_addr, 0x00, I2C_MEMADD_SIZE_8BIT, &dat, 1);
        if (status == HAL_OK)
        {
            oled_it_active = 1;
            whatToDo = 3;
        }
        else
        {
            oled_i2c_error = 1;
        }
    }
    else if (whatToDo == 1)
    {
        dat = 0xb0 + y;
        status = HAL_I2C_Mem_Write_IT(&hi2c1, oled_addr, 0x00, I2C_MEMADD_SIZE_8BIT, &dat, 1);
        if (status == HAL_OK)
        {
            oled_it_active = 1;
            whatToDo = 2;
        }
        else
        {
            oled_it_active = 0;
            whatToDo = 1; /* spróbuj ponownie w kolejnym cyklu */
        }
    }
    else if (whatToDo == 0)
    {
        status = HAL_I2C_Mem_Write_IT(&hi2c1, oled_addr, 0x40, I2C_MEMADD_SIZE_8BIT, (uint8_t *)(&oled_cache[y]), 128);
        if (status == HAL_OK)
        {
            oled_it_active = 1;
            y += 1;
            if (y > 7)
            {
                y = 0;
            }
            whatToDo = 1;
        }
        else
        {
            oled_it_active = 0;
            whatToDo = 0; /* spróbuj ponownie w kolejnym cyklu */
        }
    }
}

/* Wznawia transfer jeśli łańcuch IT się zatrzymał */
void OLED_Kick(void)
{
    if (oled_zombie) return;
    if (oled_it_active) return;
    if (hi2c1.State != HAL_I2C_STATE_READY) return;

    whatToDo = 0;

    if (HAL_I2C_Mem_Write_IT(&hi2c1, oled_addr, 0x40, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)&oled_cache[0], 128) == HAL_OK)
    {
        oled_it_active = 1;
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        oled_it_active = 0;
        oled_i2c_error = 1;
    }
}

void OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
    if (cmd == OLED_CMD)
    {
        if (oled_zombie) return;
        if (oled_i2c_error) return;  /* nie doklejaj do padniętej magistrali */

        /* Poczekaj na koniec transferu IT — Z LIMITEM, żeby padnięty
         * łańcuch nie zakręcił taska w nieskończoność. */
        uint32_t wait_start = HAL_GetTick();
        while (oled_it_active)
        {
            if (oled_i2c_error ||
                (HAL_GetTick() - wait_start) > OLED_IT_WAIT_TIMEOUT_MS)
            {
                oled_i2c_error = 1;
                return;
            }
        }

        uint8_t retries = OLED_I2C_MAX_RETRIES;
        HAL_StatusTypeDef status;
        do
        {
            status = HAL_I2C_Mem_Write(&hi2c1, oled_addr, 0x00, I2C_MEMADD_SIZE_8BIT, &dat, 1, OLED_I2C_TIMEOUT_MS);
            if (status == HAL_OK)
                return;
            /* retry od razu, bez HAL_Delay — timeout powyżej i tak wyznacza granicę */
        } while (--retries > 0);
        oled_i2c_error = 1;
    }
    else
    {
        oled_cache[point_y][point_x] = dat;
        point_x += 1;
    }
}

/* Sondowanie kandydatów z oled_addr_candidates[]. Pierwszy z ACK trafia
 * do oled_addr. Parametry trials/timeout dobiera wołający: dokładne na
 * starcie (nic innego nie działa), lekkie w tasku (recovery). */
static uint8_t oled_probe(uint8_t trials, uint8_t timeout_ms)
{
    uint8_t i;

    for (i = 0; i < OLED_ADDR_CANDIDATES_COUNT; i++) {
        uint8_t candidate = oled_addr_candidates[i];
        if (HAL_I2C_IsDeviceReady(&hi2c1, candidate, trials, timeout_ms) == HAL_OK) {
            oled_addr = candidate;
            return 1;
        }
    }
    return 0;
}

uint8_t OLED_Detect(void)
{
    return oled_probe(OLED_DET_TRIALS_BOOT, OLED_DET_TIMEOUT_BOOT_MS);
}

uint8_t OLED_GetError(void)
{
    return oled_i2c_error;
}

void OLED_ClearError(void)
{
    oled_i2c_error = 0;
}

/* Wykrywa transfer, który nie zakończył się w wyznaczonym czasie. */
void OLED_WatchdogPoll(void)
{
    if (oled_zombie || oled_i2c_error || !oled_it_active) return;

    if ((HAL_GetTick() - oled_it_started_tick) > OLED_CHAIN_STALL_MS) {
        oled_i2c_error = 1U;
    }
}

void OLED_Recover(void)
{
    if (oled_zombie) return;

    uint32_t now = HAL_GetTick();

    switch (oled_rec_state)
    {
    case OLED_REC_IDLE:
        /* Start: zatrzymaj łańcuch IT i wyczyść kursory. */
        oled_it_active = 0;
        whatToDo = 0;
        point_x = 0;
        point_y = 0;

        /* Dzielnik sondowania: przy martwym module nie zarzynaj taska —
         * pełne (lekkie) sondowanie co N cykli, pomiędzy tylko szybki powrót. */
        if ((oled_rec_probe_div++ % OLED_REC_PROBE_EVERY_N) != 0U) {
            oled_i2c_error = 1;
            return;
        }

        /* Lekkie sondowanie (polling, bez re-initu): brak modułu
         * = szybki powrót, task spróbuje w następnym oknie. */
        if (!oled_probe(OLED_DET_TRIALS_TASK, OLED_DET_TIMEOUT_TASK_MS)) {
            oled_i2c_error = 1;
            return;
        }

        HAL_I2C_DeInit(&hi2c1);
        oled_rec_tick  = now;
        oled_rec_state = OLED_REC_WAIT_AFTER_DEINIT;
        oled_i2c_error = 1;
        return;

    case OLED_REC_WAIT_AFTER_DEINIT:
        if ((now - oled_rec_tick) < 10U) {
            oled_i2c_error = 1;  /* jeszcze czekamy — bez blokowania */
            return;
        }
        HAL_I2C_Init(&hi2c1);
        oled_rec_tick  = now;
        oled_rec_state = OLED_REC_WAIT_AFTER_INIT;
        oled_i2c_error = 1;
        return;

    case OLED_REC_WAIT_AFTER_INIT:
        if ((now - oled_rec_tick) < 50U) {
            oled_i2c_error = 1;  /* jeszcze czekamy — bez blokowania */
            return;
        }
        oled_rec_state = OLED_REC_IDLE;
        if (!oled_probe(OLED_DET_TRIALS_TASK, OLED_DET_TIMEOUT_TASK_MS)) {
            oled_i2c_error = 1;
            return;
        }
        /* OLED_Init() czyści flagę błędu i kickuje łańcuch IT.
         * Gdyby init się wysypał, flaga wróci i recovery ruszy od nowa. */
        OLED_Init();
        return;

    default:
        oled_rec_state = OLED_REC_IDLE;
        oled_i2c_error = 1;
        return;
    }
}


//void OLED_Display_On(void)
//{
//    OLED_WR_Byte(0X8D, OLED_CMD); //SET DCDC命令
//    OLED_WR_Byte(0X14, OLED_CMD); //DCDC ON
//    OLED_WR_Byte(0XAF, OLED_CMD); //DISPLAY ON
//}

void OLED_Display_Off(void)
{
    display_off_step = 0;
    whatToDo = 4;
}

void OLED_Set_Pos(uint8_t x, uint8_t y)
{
    point_x = x;
    point_y = y;
}


void OLED_Clear(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
    {
        OLED_Set_Pos(0, i);
        for (n = 0; n < 128; n++)
            OLED_WR_Byte(0x00, OLED_DATA);
    }
}

//x:0~127
//y:0~7
void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
    uint8_t c = 0, i = 0;
    while (*str != '\0')
    {
        c = *str - ' '; 
        if (x > Max_Column - 1)
        {
            x = 0;
            y++;
        }
        OLED_Set_Pos(x, y);
        for (i = 0; i < 6; i++)
            OLED_WR_Byte(F6x8[c][i], OLED_DATA);
        x += 6;
        str++;
    }
}

//SSD1306
void OLED_Init(void)
{
    whatToDo = 0;
    oled_it_active = 0;
    oled_i2c_error = 0;
    point_x = 0;
    point_y = 0;

    OLED_WR_Byte(0xAE, OLED_CMD); //--display off
    OLED_WR_Byte(0x00, OLED_CMD); 
    OLED_WR_Byte(0x10, OLED_CMD); 
    OLED_WR_Byte(0x40, OLED_CMD); //--set start line address
    OLED_WR_Byte(0xB0, OLED_CMD); 
    OLED_WR_Byte(0x81, OLED_CMD); // contract control
    OLED_WR_Byte(0xf0, OLED_CMD); //--128
    OLED_WR_Byte(0xA1, OLED_CMD); //set segment remap
    OLED_WR_Byte(0xA6, OLED_CMD); 
    OLED_WR_Byte(0xA8, OLED_CMD); //--set multiplex ratio(1 to 64)
    OLED_WR_Byte(0x3F, OLED_CMD); //--1/32 duty
    OLED_WR_Byte(0xC8, OLED_CMD); 
    OLED_WR_Byte(0xD3, OLED_CMD); //-set display offset
    OLED_WR_Byte(0x00, OLED_CMD); //

    OLED_WR_Byte(0xD5, OLED_CMD); //set osc division
    OLED_WR_Byte(0x80, OLED_CMD); //

    //     OLED_WR_Byte(0xD8, OLED_CMD); //set area color mode off
    //     OLED_WR_Byte(0x05, OLED_CMD); //

    OLED_WR_Byte(0xD9, OLED_CMD); //Set Pre-Charge Period
    OLED_WR_Byte(0xF1, OLED_CMD); //

    OLED_WR_Byte(0xDA, OLED_CMD); //set com pin configuartion
    OLED_WR_Byte(0x12, OLED_CMD); //

    OLED_WR_Byte(0xDB, OLED_CMD); //set Vcomh
    OLED_WR_Byte(0x30, OLED_CMD); //

    OLED_WR_Byte(0x8D, OLED_CMD); //set charge pump enable
    OLED_WR_Byte(0x14, OLED_CMD); //

    OLED_WR_Byte(0xAF, OLED_CMD); 

    OLED_Clear();

    uint8_t i;

    OLED_showHeart(1);
    //bpm
    const uint8_t bpm[] = {
        0x3E, 0x28, 0x38, 0x00, 0x3E, 0x0A, 0x0E, 0x00, 0x3E, 0x02, 0x1C, 0x02, 0x3E};
    OLED_Set_Pos(58, 4);
    for (i = 0; i < 13; i++)
    {
        OLED_WR_Byte(bpm[i], OLED_DATA);
    }
    //%
    const uint8_t percent[] = {
        0x63, 0x33, 0x18, 0x0C, 0x66, 0x63};
    OLED_Set_Pos(121, 4);
    for (i = 0; i < 6; i++)
    {
        OLED_WR_Byte(percent[i], OLED_DATA);
    }
    //spo2
    const uint8_t spo2[] = {
        0x38, 0x44, 0x44, 0x44, 0x88, 0x00, 0xFC, 0x44, 0x44, 0x44, 0x38,
        0xE1, 0x12, 0x12, 0x12, 0xE1, 0x00, 0x23, 0x10, 0x10, 0x90, 0x60,
        0x07, 0x08, 0x08, 0x08, 0x07, 0x00, 0x0C, 0x0A, 0x09, 0x08, 0x08};
    OLED_Set_Pos(73, 2);
    for (i = 0; i < 11; i++)
    {
        OLED_WR_Byte(spo2[i], OLED_DATA);
    }
    OLED_Set_Pos(73, 3);
    for (i = 0; i < 11; i++)
    {
        OLED_WR_Byte(spo2[11 + i], OLED_DATA);
    }
    OLED_Set_Pos(73, 4);
    for (i = 0; i < 11; i++)
    {
        OLED_WR_Byte(spo2[22 + i], OLED_DATA);
    }
    OLED_ShowString(0, 0, "Zajebiste masz serce");
    OLED_ShowString(0, 1, "Serce");
    OLED_ShowString(33, 1, "Puls");
    OLED_ShowString(60, 1, "i");
    OLED_ShowString(69, 1, "Tlen");

    OLED_Kick();
}

const uint8_t heart[] = {
    0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC,
    0xF8, 0xF0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xFF, 0xFF, 0xFE, 0xFC, 0xF8,
    0x01, 0x07, 0x1F, 0x3F, 0x7F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x7F, 0x3F, 0x1F, 0x07, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03,
    0x07, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00};
void OLED_showHeart(uint8_t showOrNot)
{
    uint8_t i;
    
    OLED_Set_Pos(1, 2);
    if (showOrNot)
    {
        for (i = 0; i < 21; i++)
        {
            OLED_WR_Byte(heart[i], OLED_DATA);
        }
        OLED_Set_Pos(1, 3);
        for (i = 0; i < 21; i++)
        {
            OLED_WR_Byte(heart[21 + i], OLED_DATA);
        }
        OLED_Set_Pos(1, 4);
        for (i = 0; i < 21; i++)
        {
            OLED_WR_Byte(heart[42 + i], OLED_DATA);
        }
    }
    else
    {
        for (i = 0; i < 21; i++)
        {
            OLED_WR_Byte(0, OLED_DATA);
        }
        OLED_Set_Pos(1, 3);
        for (i = 0; i < 21; i++)
        {
            OLED_WR_Byte(0, OLED_DATA);
        }
        OLED_Set_Pos(1, 4);
        for (i = 0; i < 21; i++)
        {
            OLED_WR_Byte(0, OLED_DATA);
        }
    }
}


const uint8_t oled_nums[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x00};
const uint8_t oled_nums_pos[] = {24, 36, 47, 86, 98, 109};
void OLED_showNum(uint8_t which, uint8_t num)
{
    uint8_t dat = oled_nums[num];
    uint8_t start = oled_nums_pos[which];
    uint8_t i, temp;
    OLED_Set_Pos(start, 2);
    for (i = 0; i < 10; i++)
    {
        temp = 0;
        if (i <= 1)
        {
            if (dat & OLED_NUM_F)
                temp |= 0xf8;
        }
        else if (i >= 2 && i <= 7)
        {
            if (dat & OLED_NUM_A)
                temp |= 0x06;
        }
        else
        {
            if (dat & OLED_NUM_B)
                temp |= 0xf8;
        }
        OLED_WR_Byte(temp, OLED_DATA);
    }
    OLED_Set_Pos(start, 3);
    for (i = 0; i < 10; i++)
    {
        temp = 0;
        if (i <= 1)
        {
            if (dat & OLED_NUM_F)
                temp |= 0x03;
            if (dat & OLED_NUM_E)
                temp |= 0xf0;
        }
        else if (i >= 2 && i <= 7)
        {
            if (dat & OLED_NUM_G)
                temp |= 0x0c;
        }
        else
        {
            if (dat & OLED_NUM_B)
                temp |= 0x03;
            if (dat & OLED_NUM_C)
                temp |= 0xf0;
        }
        OLED_WR_Byte(temp, OLED_DATA);
    }
    OLED_Set_Pos(start, 4);
    for (i = 0; i < 10; i++)
    {
        temp = 0;
        if (i <= 1)
        {
            if (dat & OLED_NUM_E)
                temp |= 0x07;
        }
        else if (i >= 2 && i <= 7)
        {
            if (dat & OLED_NUM_D)
                temp |= 0x18;
        }
        else
        {
            if (dat & OLED_NUM_C)
                temp |= 0x07;
        }
        OLED_WR_Byte(temp, OLED_DATA);
    }
}

// UWAGA ZMIANA NOWA FUNKCJA 
void OLED_showNumAt(uint8_t x_start, uint8_t num)
{
    uint8_t dat   = oled_nums[num];
    uint8_t start = x_start;     // zamiast oled_nums_pos[which]
    uint8_t i, temp;

    OLED_Set_Pos(start, 3);
    for (i = 0; i < 10; i++)
    {
        temp = 0;
        if (i <= 1)
        {
            if (dat & OLED_NUM_F)
                temp |= 0xf8;
        }
        else if (i >= 2 && i <= 7)
        {
            if (dat & OLED_NUM_A)
                temp |= 0x06;
        }
        else
        {
            if (dat & OLED_NUM_B)
                temp |= 0xf8;
        }
        OLED_WR_Byte(temp, OLED_DATA);
    }

    OLED_Set_Pos(start, 4);
    for (i = 0; i < 10; i++)
    {
        temp = 0;
        if (i <= 1)
        {
            if (dat & OLED_NUM_F)
                temp |= 0x03;
            if (dat & OLED_NUM_E)
                temp |= 0xf0;
        }
        else if (i >= 2 && i <= 7)
        {
            if (dat & OLED_NUM_G)
                temp |= 0x0c;
        }
        else
        {
            if (dat & OLED_NUM_B)
                temp |= 0x03;
            if (dat & OLED_NUM_C)
                temp |= 0xf0;
        }
        OLED_WR_Byte(temp, OLED_DATA);
    }

    OLED_Set_Pos(start, 5);
    for (i = 0; i < 10; i++)
    {
        temp = 0;
        if (i <= 1)
        {
            if (dat & OLED_NUM_E)
                temp |= 0x07;
        }
        else if (i >= 2 && i <= 7)
        {
            if (dat & OLED_NUM_D)
                temp |= 0x18;
        }
        else
        {
            if (dat & OLED_NUM_C)
                temp |= 0x07;
        }
        OLED_WR_Byte(temp, OLED_DATA);
    }
}


//128*24
uint8_t pos_x_this = 0;
char pos_y_old = 0;
char max(char a, char b) { return a > b ? a : b; }
char min(char a, char b) { return a < b ? a : b; }
void OLED_drawChart(float value)
{
    uint8_t dat[] = {0xff, 0xff, 0xff};
    char pos_y = (char)(value * 0.12) + 10;
    if (pos_y > 23)
        pos_y = 23;
    if (pos_y <= 0)
        pos_y = 0;
    uint8_t i, ii, blank;
    char y_max = max(pos_y, pos_y_old);
    char y_min = min(pos_y, pos_y_old);

    if (y_max == y_min)
        y_max = y_min + 1;
    for (i = 0; i < 3; i++)
    {
        if ((y_min - i * 8) >= 0)
            dat[i] &= (uint8_t)(dat[i] >> (y_min - i * 8));
        if (((i + 1) * 8 - y_max) >= 0)
            dat[i] &= (uint8_t)(dat[i] << ((i + 1) * 8 - y_max));
    }
    for (i = 0; i < 3; i++)
    {
        OLED_Set_Pos(pos_x_this, 7 - i);
        OLED_WR_Byte(dat[i], OLED_DATA);
        blank = 127 - pos_x_this;
        if (blank > 3)
            blank = 3;
        for (ii = 0; ii < blank; ii++)
            OLED_WR_Byte(0, OLED_DATA);
    }
    pos_y_old = pos_y;
    pos_x_this++;
    if (pos_x_this > 127)
        pos_x_this = 0;
}
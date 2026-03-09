/*=======================================================================
 * DMX512 Transmitter for PSoC6 - Gecorrigeerde versie
 * Hardware: PSoC6 WiFi-BT Pioneer Kit + Waveshare Pico-2CH-RS485
 * DMX TX Pin: P12_1 (SCB6 TX) - verbinden met TX_CH0 van RS485 module
 *=======================================================================*/

/*----------------------------------------------------------------------
 * Includes
 *---------------------------------------------------------------------*/
#include "cybsp.h"
#include "cyhal.h"
#include "cy_pdl.h"
#include "cycfg_peripherals.h"
#include <stdbool.h>
#include <stdint.h>

/*----------------------------------------------------------------------
 * DMX Protocol Defines
 *---------------------------------------------------------------------*/
#define DMX_UNIVERSE_SIZE       513
#define DMX_START_CODE          0x00
#define DMX_BAUDRATE            250000
#define DMX_REFRESH_MS          30
#define DMX_BREAK_US            200
#define DMX_MAB_US              12

/*----------------------------------------------------------------------
 * Hardware Defines
 *---------------------------------------------------------------------*/
#define DMX_TX_PIN              P12_1      // SCB6 TX op P12_1
#define PICOSPOT_ADDRESS        1

/*----------------------------------------------------------------------
 * UART Hardware
 *---------------------------------------------------------------------*/
static cyhal_uart_t uart_obj;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

/*----------------------------------------------------------------------
 * DMX Buffer Initialisatie
 *---------------------------------------------------------------------*/
static void dmx_init_buffer(void)
{
    dmx_buffer[0] = DMX_START_CODE;
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

/*----------------------------------------------------------------------
 * DMX Kanaal Waarde Instellen
 *---------------------------------------------------------------------*/
static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch < 1 || ch > 512) return;
    dmx_buffer[ch] = value;
}

/*----------------------------------------------------------------------
 * BREAK en MAB Generatie met directe GPIO controle
 *---------------------------------------------------------------------*/
static void dmx_send_break_mab(void)
{
    uint32_t int_state;
    
    /* Interrupts uitschakelen voor timing */
    int_state = Cy_SysLib_EnterCriticalSection();
    
    /* UART uitschakelen */
    cyhal_uart_free(&uart_obj);
    
    /* Pin als GPIO voor BREAK */
    cyhal_gpio_configure(DMX_TX_PIN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG);
    
    /* BREAK - 200µs laag */
    cyhal_gpio_write(DMX_TX_PIN, 0);
    Cy_SysLib_DelayUs(DMX_BREAK_US);
    
    /* MAB - 12µs hoog */
    cyhal_gpio_write(DMX_TX_PIN, 1);
    Cy_SysLib_DelayUs(DMX_MAB_US);
    
    /* GPIO vrijgeven */
    cyhal_gpio_free(DMX_TX_PIN);
    
    /* Interrupts hervatten */
    Cy_SysLib_ExitCriticalSection(int_state);
}

/*----------------------------------------------------------------------
 * UART Initialisatie met HAL
 *---------------------------------------------------------------------*/
static cy_rslt_t dmx_uart_init(void)
{
    cy_rslt_t result;
    
    /* UART configuratie voor DMX */
    const cyhal_uart_cfg_t uart_config = {
        .data_bits = 8,
        .stop_bits = 2,
        .parity = CYHAL_UART_PARITY_NONE,
        .rx_buffer = NULL,
        .rx_buffer_size = 0
    };
    
    /* Initialiseer UART op TX pin (geen RX, CTS, RTS) */
    result = cyhal_uart_init(&uart_obj, CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, NC, NC, NULL, &uart_config);
    if (result != CY_RSLT_SUCCESS)
        return 0xDEAD0001;
    
    /* Stel baudrate in */
    result = cyhal_uart_set_baud(&uart_obj, DMX_BAUDRATE, NULL);
    if (result != CY_RSLT_SUCCESS)
        return 0xDEAD0002;
    
    return CY_RSLT_SUCCESS;
}

/*----------------------------------------------------------------------
 * DMX Frame Verzenden
 *---------------------------------------------------------------------*/
static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;
    cy_rslt_t result;
    
    /* Genereer BREAK en MAB */
    dmx_send_break_mab();
    
    /* Initialiseer UART opnieuw */
    result = dmx_uart_init();
    if (result != CY_RSLT_SUCCESS)
    {
        cyhal_gpio_toggle(CYBSP_USER_LED);
        return;
    }
    
    /* Verstuur DMX data */
    cyhal_uart_write(&uart_obj, dmx_buffer, &len);
}

/*----------------------------------------------------------------------
 * Error LED Patterns
 *---------------------------------------------------------------------*/
static void error_led_pattern(uint32_t error_code)
{
    cyhal_gpio_write(CYBSP_USER_LED, 1);
    
    switch(error_code)
    {
        case 0xDEAD0001:
            for(;;)
            {
                cyhal_gpio_write(CYBSP_USER_LED, 0);
                Cy_SysLib_Delay(100);
                cyhal_gpio_write(CYBSP_USER_LED, 1);
                Cy_SysLib_Delay(100);
            }
            break;
            
        case 0xDEAD0002:
            for(;;)
            {
                cyhal_gpio_write(CYBSP_USER_LED, 0);
                Cy_SysLib_Delay(500);
                cyhal_gpio_write(CYBSP_USER_LED, 1);
                Cy_SysLib_Delay(500);
            }
            break;
            
        default:
            for(;;)
            {
                cyhal_gpio_toggle(CYBSP_USER_LED);
                Cy_SysLib_Delay(250);
            }
            break;
    }
}

/*----------------------------------------------------------------------
 * Main Functie
 *---------------------------------------------------------------------*/
int main(void)
{
    cy_rslt_t result;
    
    /* Initialiseer board */
    cybsp_init();
    __enable_irq();
    
    /* Debug LED op Pioneer Kit */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
    
    /* DMX buffer initialiseren */
    dmx_init_buffer();
    
    /* UART initialiseren */
    result = dmx_uart_init();
    if (result != CY_RSLT_SUCCESS)
    {
        error_led_pattern(result);
    }
    
    /* Testwaarden voor Picospot LED */
    uint16_t addr = PICOSPOT_ADDRESS;
    
    dmx_set_channel(addr + 0, 128);   /* Pan - midden */
    dmx_set_channel(addr + 1, 128);   /* Tilt - midden */
    dmx_set_channel(addr + 2, 0);     /* Pan fine */
    dmx_set_channel(addr + 3, 0);     /* Tilt fine */
    dmx_set_channel(addr + 4, 0);     /* Pan/Tilt speed */
    dmx_set_channel(addr + 5, 0);     /* Color wheel - wit */
    dmx_set_channel(addr + 6, 0);     /* Gobo wheel - open */
    dmx_set_channel(addr + 7, 255);   /* Dimmer - vol open */
    dmx_set_channel(addr + 8, 0);     /* Shutter - open */
    
    /* Simpele animatie */
    uint8_t color_value = 0;
    int color_dir = 1;
    uint32_t frame_count = 0;
    
    /* Hoofdloop */
    for (;;)
    {
        /* Verstuur DMX frame */
        dmx_send_frame();
        
        /* Kleur animatie elke 10 frames */
        if ((frame_count % 10) == 0)
        {
            color_value += color_dir;
            if (color_value >= 255 || color_value <= 0)
                color_dir = -color_dir;
            
            dmx_set_channel(addr + 5, color_value);
        }
        
        /* LED toggle voor feedback */
        cyhal_gpio_toggle(CYBSP_USER_LED);
        
        /* Wacht voor volgende frame */
        Cy_SysLib_Delay(DMX_REFRESH_MS);
        
        frame_count++;
    }
}
/*
 * Copyright (C) 2017 Freie Universität Berlin
 *               2017 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     boards_3demo
 * @{
 *
 * @file
 * @brief       Peripheral MCU configuration for the 3demo (nucleo-l073rz) board
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 * @author      Jan Schlichter
 */

#ifndef PERIPH_CONF_H
#define PERIPH_CONF_H

#include "periph_cpu.h"
#include "cfg_i2c1_pb8_pb9.h"
#include "cfg_rtt_default.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @name    at86rf215 configuration
 * @{
 */
#define AT86RF215_PARAM_SPI        SPI_DEV(0)
#define AT86RF215_PARAM_CS         GPIO_PIN(PORT_B, 6)
#define AT86RF215_PARAM_INT        GPIO_PIN(PORT_A, 8)
#define AT86RF215_PARAM_RESET      GPIO_PIN(PORT_B, 5)
#define AT86RF215_PARAM_SPI_CLK    SPI_CLK_1MHZ
/** @} */

/**
 * @name    BMX680configuration
 * @{
 */
#define BMX280_PARAM_I2C_DEV         I2C_DEV(0)
/** @} */

/**
 * @name    SDP3x configuration
 * @{
 */
 //TODO up to 3 sensors can be connected
#define SDP3X_PARAM_I2C_DEV         I2C_DEV(0)
#define SDP3X_PARAM_I2C_ADDR        SDP3X_ADDR1
#define SDP3X_PARAM_MODEL    SDP3X_MODEL_31
/** @} */

/**
 * @name    Clock system configuration
 * @{
 */
/* 0: no external high speed crystal available
 * else: actual crystal frequency [in Hz] */
#define CLOCK_HSE           (0)

#ifndef CLOCK_LSE
/* 0: no external low speed crystal available,
 * 1: external crystal available (always 32.768kHz)
 * This defaults to 0 because hardware revision 'MB1136 C-01' of the nucleo-64
 * board disconnects LSE by default. You may safely set this to 1 on revisions
 * newer than 'MB1136 C-01' */
#define CLOCK_LSE           (0)
#endif

/* 0: enable MSI only if HSE isn't available
 * 1: always enable MSI (e.g. if USB or RNG is used)*/
#define CLOCK_MSI_ENABLE    (1)

#ifndef CLOCK_MSI_LSE_PLL
/* 0: disable Hardware auto calibration with LSE
 * 1: enable Hardware auto calibration with LSE (PLL-mode)
 * Same as with CLOCK_LSE above this defaults to 0 because LSE is
 * mandatory for MSI/LSE-trimming to work */
#define CLOCK_MSI_LSE_PLL   (0)
#endif

/* give the target core clock (HCLK) frequency [in Hz], maximum: 80MHz */
#define CLOCK_CORECLOCK     (80000000U)
/* PLL configuration: make sure your values are legit!
 *
 * compute by: CORECLOCK = (((PLL_IN / M) * N) / R)
 * with:
 * PLL_IN:  input clock, HSE or MSI @ 48MHz
 * M:       pre-divider,  allowed range: [1:8]
 * N:       multiplier,   allowed range: [8:86]
 * R:       post-divider, allowed range: [2,4,6,8]
 *
 * Also the following constraints need to be met:
 * (PLL_IN / M)     -> [4MHz:16MHz]
 * (PLL_IN / M) * N -> [64MHz:344MHz]
 * CORECLOCK        -> 80MHz MAX!
 */
#define CLOCK_PLL_M         (6)
#define CLOCK_PLL_N         (20)
#define CLOCK_PLL_R         (2)
/* peripheral clock setup */
#define CLOCK_AHB_DIV       RCC_CFGR_HPRE_DIV1
#define CLOCK_AHB           (CLOCK_CORECLOCK / 1)
#define CLOCK_APB1_DIV      RCC_CFGR_PPRE1_DIV4
#define CLOCK_APB1          (CLOCK_CORECLOCK / 4)
#define CLOCK_APB2_DIV      RCC_CFGR_PPRE2_DIV2
#define CLOCK_APB2          (CLOCK_CORECLOCK / 2)
/** @} */

/**
 * @name    DMA streams configuration
 * @{
 */
#ifdef MODULE_PERIPH_DMA
static const dma_conf_t dma_config[] = {
    { .stream = 1 },    /* DMA1 Channel 2 - SPI1_RX | USART3_TX */
    { .stream = 2 },    /* DMA1 Channel 3 - SPI1_TX */
    { .stream = 3 },    /* DMA1 Channel 4 - USART1_TX */
    { .stream = 6 },    /* DMA1 Channel 7 - USART2_TX */
};

#define DMA_0_ISR  isr_dma1_channel2
#define DMA_1_ISR  isr_dma1_channel3
#define DMA_2_ISR  isr_dma1_channel4
#define DMA_3_ISR  isr_dma1_channel7

#define DMA_NUMOF           ARRAY_SIZE(dma_config)
#endif /* MODULE_PERIPH_DMA */
/** @} */


/**
 * @name    Timer configuration
 * @{
 */
static const timer_conf_t timer_config[] = {
    {
        .dev      = TIM5,
        .max      = 0xffffffff,
        .rcc_mask = RCC_APB1ENR1_TIM5EN,
        .bus      = APB1,
        .irqn     = TIM5_IRQn
    }
};

#define TIMER_0_ISR         isr_tim5

#define TIMER_NUMOF         ARRAY_SIZE(timer_config)
/** @} */

/**
 * @name    UART configuration
 * @{
 */
static const uart_conf_t uart_config[] = {
    {
        .dev        = USART2,
        .rcc_mask   = RCC_APB1ENR1_USART2EN,
        .rx_pin     = GPIO_PIN(PORT_A, 3),
        .tx_pin     = GPIO_PIN(PORT_A, 2),
        .rx_af      = GPIO_AF7,
        .tx_af      = GPIO_AF7,
        .bus        = APB1,
        .irqn       = USART2_IRQn,
        .type       = STM32_USART,
        .clk_src    = 0, /* Use APB clock */
#ifdef MODULE_PERIPH_DMA
        .dma        = 3,
        .dma_chan   = 2
#endif
    },
    {
        .dev        = USART3,
        .rcc_mask   = RCC_APB1ENR1_USART3EN,
        .rx_pin     = GPIO_PIN(PORT_C, 11),
        .tx_pin     = GPIO_PIN(PORT_C, 10),
        .rx_af      = GPIO_AF7,
        .tx_af      = GPIO_AF7,
        .bus        = APB1,
        .irqn       = USART3_IRQn,
        .type       = STM32_USART,
        .clk_src    = 0, /* Use APB clock */
#ifdef MODULE_PERIPH_DMA
        .dma        = 0,
        .dma_chan   = 2
#endif
    },
    {
        .dev        = USART1,
        .rcc_mask   = RCC_APB2ENR_USART1EN,
        .rx_pin     = GPIO_PIN(PORT_A, 10),
        .tx_pin     = GPIO_PIN(PORT_A, 9),
        .rx_af      = GPIO_AF7,
        .tx_af      = GPIO_AF7,
        .bus        = APB2,
        .irqn       = USART1_IRQn,
        .type       = STM32_USART,
        .clk_src    = 0, /* Use APB clock */
#ifdef MODULE_PERIPH_DMA
        .dma        = 2,
        .dma_chan   = 2
#endif
    }
};

#define UART_0_ISR          (isr_usart2)
#define UART_1_ISR          (isr_usart3)
#define UART_2_ISR          (isr_usart1)

#define UART_NUMOF          ARRAY_SIZE(uart_config)
/** @} */

/**
 * @name    PWM configuration
 * @{
 */
static const pwm_conf_t pwm_config[] = {
    {
        .dev      = TIM2,
        .rcc_mask = RCC_APB1ENR1_TIM2EN,
        .chan     = { { .pin = GPIO_PIN(PORT_A, 15), .cc_chan = 0},
                      { .pin = GPIO_PIN(PORT_B,  3), .cc_chan = 1},
                      { .pin = GPIO_PIN(PORT_B, 10), .cc_chan = 2},
                      { .pin = GPIO_PIN(PORT_B, 11), .cc_chan = 3} },
        .af       = GPIO_AF1,
        .bus      = APB1
    },
    {
        .dev      = TIM3,
        .rcc_mask = RCC_APB1ENR1_TIM3EN,
        .chan     = { { .pin = GPIO_PIN(PORT_B, 4), .cc_chan = 0 },
                      { .pin = GPIO_UNDEF, .cc_chan = 0 },
                      { .pin = GPIO_UNDEF, .cc_chan = 0 },
                      { .pin = GPIO_UNDEF, .cc_chan = 0 } },
        .af       = GPIO_AF2,
        .bus      = APB1
    },
    {
        .dev      = TIM8,
        .rcc_mask = RCC_APB2ENR_TIM8EN,
        .chan     = { { .pin = GPIO_PIN(PORT_C, 6), .cc_chan = 0},
                      { .pin = GPIO_PIN(PORT_C, 7), .cc_chan = 1},
                      { .pin = GPIO_PIN(PORT_C, 8), .cc_chan = 2},
                      { .pin = GPIO_PIN(PORT_C, 9), .cc_chan = 3} },
        .af       = GPIO_AF3,
        .bus      = APB2
    }
};

#define PWM_NUMOF           ARRAY_SIZE(pwm_config)
/** @} */

/**
 * @name    SPI configuration
 *
 * @note    The spi_divtable is auto-generated from
 *          `cpu/stm32_common/dist/spi_divtable/spi_divtable.c`
 * @{
 */
static const uint8_t spi_divtable[2][5] = {
    {       /* for APB1 @ 20000000Hz */
        7,  /* -> 78125Hz */
        5,  /* -> 312500Hz */
        3,  /* -> 1250000Hz */
        1,  /* -> 5000000Hz */
        0   /* -> 10000000Hz */
    },
    {       /* for APB2 @ 40000000Hz */
        7,  /* -> 156250Hz */
        6,  /* -> 312500Hz */
        4,  /* -> 1250000Hz */
        2,  /* -> 5000000Hz */
        1   /* -> 10000000Hz */
    }
};

static const spi_conf_t spi_config[] = {
    {
        .dev      = SPI1,
        .mosi_pin = GPIO_PIN(PORT_A, 7),
        .miso_pin = GPIO_PIN(PORT_A, 6),
        .sclk_pin = GPIO_PIN(PORT_A, 5),
        .cs_pin   = GPIO_UNDEF,
        .mosi_af  = GPIO_AF5,
        .miso_af  = GPIO_AF5,
        .sclk_af  = GPIO_AF5,
        .cs_af    = GPIO_AF5,
        .rccmask  = RCC_APB2ENR_SPI1EN,
        .apbbus   = APB2,
#ifdef MODULE_PERIPH_DMA
        .tx_dma   = 1,
        .tx_dma_chan = 1,
        .rx_dma   = 0,
        .rx_dma_chan = 1,
#endif
    }
};

#define SPI_NUMOF           ARRAY_SIZE(spi_config)
/** @} */

/**
 * @name    ADC configuration
 *
 * configure only  ADC channels for the Arduino header pins A0-A5
 *
 * @{
 */
#define ADC_NUMOF           (6U)
#define ADC_CONFIG {             \
    {GPIO_PIN(PORT_A, 0), 0, 5},  /*< ADC12_IN5 */   \
    {GPIO_PIN(PORT_A, 1), 0, 6},  /*< ADC12_IN6 */   \
    {GPIO_PIN(PORT_A, 4), 1, 9},  /*< ADC12_IN9 */   \
    {GPIO_PIN(PORT_B, 0), 1, 15}, /*< ADC12_IN15 */  \
    {GPIO_PIN(PORT_C, 1), 2, 2},  /*< ADC123_IN_2 */ \
    {GPIO_PIN(PORT_C, 0), 2, 1},  /*< ADC123_IN_1 */ \
}
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_CONF_H */
/** @} */

#include <string.h>

#include "pico/stdlib.h"

#include "ff.h"
#include "diskio.h"
#include "hw_config.h"
#include "my_debug.h"

void spi0_dma_isr(void);

static spi_t spis[] = {
    {
        .hw_inst = spi0,
        .miso_gpio = 16,
        .mosi_gpio = 19,
        .sck_gpio = 18,
        .baud_rate = 12500 * 1000,
        .dma_isr = spi0_dma_isr,
    },
};

static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",
        .spi = &spis[0],
        .ss_gpio = 17,
        .use_card_detect = false,
        .card_detect_gpio = 0,
        .card_detected_true = 0,
        .m_Status = STA_NOINIT,
    },
};

void spi0_dma_isr(void)
{
    spi_irq_handler(&spis[0]);
}

size_t sd_get_num(void)
{
    return count_of(sd_cards);
}

sd_card_t *sd_get_by_num(size_t num)
{
    if (num < sd_get_num()) {
        return &sd_cards[num];
    }
    return NULL;
}

size_t spi_get_num(void)
{
    return count_of(spis);
}

spi_t *spi_get_by_num(size_t num)
{
    if (num < spi_get_num()) {
        return &spis[num];
    }
    return NULL;
}

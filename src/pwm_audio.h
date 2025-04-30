#include <stdint.h>
#include <stdlib.h>
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

typedef void (*DmaFinishedHandler)(void);

typedef uint32_t sample_t;

typedef struct {
    int PIN;
    int SLICE_NUM;
    int SAMPLE_BITS;
    float FREQUENCY;
    int LATENCY;
    DmaFinishedHandler dma_handler;

    sample_t *buff;
    int dma_ch;
    int playing_bank;
} PwmAudio;

static PwmAudio *inst = NULL;

PwmAudio* pwm_audio_init(int pin, int latency, int sample_bits, float freq_ratio, DmaFinishedHandler handler) {
    PwmAudio *audio = (PwmAudio*)malloc(sizeof(PwmAudio));
    audio->PIN = pin;
    audio->SLICE_NUM = pwm_gpio_to_slice_num(pin);
    audio->SAMPLE_BITS = sample_bits;
    audio->FREQUENCY = freq_ratio;
    audio->LATENCY = latency;
    audio->dma_handler = handler;
    audio->buff = (sample_t*)malloc(sizeof(sample_t) * latency * 2); // double buffer
    audio->playing_bank = 0;

    gpio_set_function(audio->PIN, GPIO_FUNC_PWM);

    int pwm_period = 1 << audio->SAMPLE_BITS;
    float pwm_clkdiv = freq_ratio / pwm_period;

    pwm_config pwm_cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&pwm_cfg, pwm_clkdiv);
    pwm_config_set_wrap(&pwm_cfg, pwm_period - 1);
    pwm_init(audio->SLICE_NUM, &pwm_cfg, true);

    pwm_set_gpio_level(audio->PIN, 1 << (audio->SAMPLE_BITS - 1));

    inst = audio;
    return audio;
}

void pwm_audio_start_dma(PwmAudio *audio) {
    dma_channel_config dma_cfg = dma_channel_get_default_config(audio->dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_PWM_WRAP0 + audio->SLICE_NUM);

    dma_channel_configure(
        audio->dma_ch,
        &dma_cfg,
        &pwm_hw->slice[audio->SLICE_NUM].cc,  // write addr
        audio->buff + (audio->playing_bank * audio->LATENCY),  // read addr
        audio->LATENCY,
        true
    );
}

void pwm_audio_play(PwmAudio *audio) {
    audio->dma_ch = dma_claim_unused_channel(true);
    dma_channel_set_irq0_enabled(audio->dma_ch, true);
    irq_set_exclusive_handler(DMA_IRQ_0, audio->dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    audio->playing_bank = 0;
    pwm_audio_start_dma(audio);
}

void pwm_audio_stop(PwmAudio *audio) {
    dma_channel_unclaim(audio->dma_ch);
    pwm_set_gpio_level(audio->PIN, 1 << (audio->SAMPLE_BITS - 1));
}

void pwm_audio_flip_buffer(PwmAudio *audio) {
    int fill_bank = audio->playing_bank;
    audio->playing_bank = (audio->playing_bank + 1) & 1;
    pwm_audio_start_dma(audio);
    dma_hw->ints0 = (1u << audio->dma_ch);
}

sample_t* pwm_audio_get_buffer(PwmAudio *audio, int bank) {
    return audio->buff + (bank * audio->LATENCY);
}

sample_t* pwm_audio_get_next_buffer(PwmAudio *audio) {
    int next_bank = (audio->playing_bank + 1) & 1;
    return pwm_audio_get_buffer(audio, next_bank);
}
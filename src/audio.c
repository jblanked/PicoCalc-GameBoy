#include "audio.h"
#include "shared.h"
#include "hedley.h"
#include "../ext/PicoAudio/audio.h"

queue_t call_queue;
int16_t *stream;

void audio_process(void)
{
#if PICO_RP2350
    flash_safe_execute_core_init();
#endif

    /* Allocate memory for the stream buffer */
    stream = malloc(AUDIO_SAMPLES_TOTAL * sizeof(int16_t));
    assert(stream != NULL);
    memset(stream, 0, AUDIO_SAMPLES_TOTAL * sizeof(int16_t));

    /* Initialize I2S sound driver (using PIO0) */
    i2s_config_t i2s_config = i2s_get_default_config();
    i2s_config.sample_freq = AUDIO_SAMPLE_RATE;
    i2s_config.dma_trans_count = AUDIO_SAMPLES;
    i2s_volume(&i2s_config, 4);
    i2s_init(&i2s_config);

    /* Initialize audio emulation. */
    audio_init(&apu_ctx);

    DBG_INFO("I Audio ready on core1.\n");

    while (1)
    {
        audio_commands_e cmd;
        queue_remove_blocking(&call_queue, &cmd);
        switch (cmd)
        {
        case AUDIO_CMD_PLAYBACK:
            audio_callback(&apu_ctx, stream);
            i2s_dma_write(&i2s_config, stream);
            break;

        case AUDIO_CMD_VOLUME_UP:
            // flash_safe_execute_core_deinit();
            i2s_increase_volume(&i2s_config);
            break;

        case AUDIO_CMD_VOLUME_DOWN:
            i2s_decrease_volume(&i2s_config);
            break;

        default:
            break;
        }
    }

    DBG_INFO("I Audio stop on core1.\n");
    HEDLEY_UNREACHABLE();
}
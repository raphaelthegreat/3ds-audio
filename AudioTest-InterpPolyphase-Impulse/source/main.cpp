#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cfenv>

#include <3ds.h>

#include "audio.h"

// Impuse function, mono PCM16
void fillBuffer(s16 *audio_buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        audio_buffer[i] = 0;
    }
    audio_buffer[0] = 0x7FFF;

    DSP_FlushDataCache(audio_buffer, size);
}

void waitForKey() {
    while (aptMainLoop()) {
        gfxSwapBuffers();
        gfxFlushBuffers();
        gspWaitForVBlank();

        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown)
            break;
    }
}

int main(int argc, char **argv) {
    gfxInitDefault();

    PrintConsole botScreen;
    PrintConsole topScreen;

    consoleInit(GFX_TOP, &topScreen);
    consoleInit(GFX_BOTTOM, &botScreen);
    consoleSelect(&topScreen);

    unsigned coefficient_select = 0;
    printf("[A]: coefficients 0 \n");
    printf("[B]: coefficients 1 \n");
    printf("[X]: coefficients 2 \n");
    printf("[Y]: coefficients 3 \n");
    while (aptMainLoop()) {
        gfxSwapBuffers();
        gfxFlushBuffers();
        gspWaitForVBlank();

        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_A){
            coefficient_select = 0;
            break;
        }
        if (kDown & KEY_B){
            coefficient_select = 1;
            break;
        }
        if (kDown & KEY_X){
            coefficient_select = 2;
            break;
        }
        if (kDown & KEY_Y){
            coefficient_select = 3;
            break;
        }
    }
    printf("coefficients = %u\n", coefficient_select);

    srand(time(nullptr));

    constexpr size_t NUM_SAMPLES = 160*200;
    s16 *audio_buffer = (s16*)linearAlloc(NUM_SAMPLES * sizeof(s16));
    fillBuffer(audio_buffer, NUM_SAMPLES);

    AudioState state;
    {
        auto dspfirm = loadDspFirmFromFile();
        if (!dspfirm) {
            printf("Couldn't load firmware\n");
            goto end;
        }
        auto ret = audioInit(*dspfirm);
        if (!ret) {
            printf("Couldn't init audio\n");
            goto end;
        }
        state = *ret;
    }

    {
        float rate_multiplier = 0.025f;
        printf("rate_multiplier = %f\n", rate_multiplier);

        state.waitForSync();
        initSharedMem(state);
        state.write().dsp_configuration->mixer1_enabled_dirty = true;
        state.write().dsp_configuration->mixer1_enabled = true;
        state.write().source_configurations->config[0].gain[1][0] = 1.0;
        state.write().source_configurations->config[0].gain_1_dirty = true;
        state.notifyDsp();
        printf("init\n");

        bool entered = false;
        bool passed = true;
        {
            u16 buffer_id = 0;

            state.write().source_configurations->config[0].play_position = 0;
            state.write().source_configurations->config[0].physical_address = osConvertVirtToPhys(audio_buffer);
            state.write().source_configurations->config[0].length = NUM_SAMPLES;
            state.write().source_configurations->config[0].mono_or_stereo = DSP::HLE::SourceConfiguration::Configuration::MonoOrStereo::Mono;
            state.write().source_configurations->config[0].format = DSP::HLE::SourceConfiguration::Configuration::Format::PCM16;
            state.write().source_configurations->config[0].fade_in = false;
            state.write().source_configurations->config[0].adpcm_dirty = false;
            state.write().source_configurations->config[0].is_looping = false;
            state.write().source_configurations->config[0].buffer_id = ++buffer_id;
            state.write().source_configurations->config[0].partial_reset_flag = true;
            state.write().source_configurations->config[0].play_position_dirty = true;
            state.write().source_configurations->config[0].embedded_buffer_dirty = true;

            state.write().source_configurations->config[0].enable = true;
            state.write().source_configurations->config[0].enable_dirty = true;

            state.write().source_configurations->config[0].rate_multiplier = rate_multiplier;
            state.write().source_configurations->config[0].rate_multiplier_dirty = true;
            state.write().source_configurations->config[0].interpolation_mode = DSP::HLE::SourceConfiguration::Configuration::InterpolationMode::Polyphase;
            state.write().source_configurations->config[0].interpolation_related = coefficient_select;
            state.write().source_configurations->config[0].interpolation_dirty = true;

            state.notifyDsp();

            bool continue_reading = true;
            for (size_t frame_count = 0; continue_reading && frame_count < 10; frame_count++) {
                state.waitForSync();

                for (size_t i = 0; i < 160; i++) {
                    if (state.write().intermediate_mix_samples->mix1.pcm32[0][i]) {
                        entered = true;
                        printf("[intermediate] frame=%i, sample=%i\n", frame_count, i);
                        for (size_t j = 0; j < 120; j++) {
                            s32 real = (s32)state.write().intermediate_mix_samples->mix1.pcm32[0][j];
                            printf("%08lx ", (u32)real);
                        }
                        continue_reading = false;
                        printf("\n");
                        break;
                    }
                }

                state.notifyDsp();
            }

            printf("Done!\n");
            if (entered && passed) {
                printf("Test passed!\n");
            } else {
                printf("FAIL\n");
            }
        }
    }

end:
    audioExit(state);
    waitForKey();
    gfxExit();
    return 0;
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <3ds.h>

#include "audio.h"

// High frequency square wave, PCM16
void fillBuffer(u32 *audio_buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        u32 data = (i % 2 == 0 ? 0x1000 : 0x2000);
        audio_buffer[i] = (data<<16) | (data&0xFFFF);
    }

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

    constexpr size_t NUM_SAMPLES = 160*200;
    u32 *audio_buffer = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
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
        const s16 simple_b0 = 0.7 * (1 << 15);
        const s16 simple_a1 = 0.3 * (1 << 15);
        const s16 biquad_b0 = 0.057200221035302035 * (1 << 14);
        const s16 biquad_b1 = 0.11440044207060407 * (1 << 14);
        const s16 biquad_b2 = 0.057200221035302035 * (1 << 14);
        const s16 biquad_a1 = -1.2188761083637 * (1 << 14);
        const s16 biquad_a2 = 0.44767699250490806 * (1 << 14);

        array<s32, 160> expected_output;
        {
            s32 x1 = 0;
            s32 x2 = 0;
            s32 y1 = 0;
            s32 y2 = 0;
            for (int i=0; i<160; i++) {
                const s32 x0 = (i % 4 == 0 || i % 4 == 1 ? 0x1000 : 0x2000);
                s32 y0 = ((s32)x0 * (s32)biquad_b0 + (s32)x1 * biquad_b1 + (s32)x2 * biquad_b2 + (s32)biquad_a1 * y1 + (s32)biquad_a2 * y2) >> 14;
                if (y0 >= 32767) y0 = 32767;
                if (y0 <= -32768) y0 = -32768;
                expected_output[i] = y0;

                x2 = x1;
                x1 = x0;
                y2 = y1;
                y1 = y0;
            }
        }

        {
            s32 y1 = 0;
            for (int i=0; i<160; i++) {
                const s32 x0 = expected_output[i];
                s32 y0 = ((s32)x0 * (s32)simple_b0 + (s32)simple_a1 * y1) >> 15;
                if (y0 >= 32767) y0 = 32767;
                if (y0 <= -32768) y0 = -32768;
                expected_output[i] = y0;
                y1 = y0;
            }
        }

        // Two sample delay for no good reason.
        {
            for (int i=160; i>=2; i--) {
                expected_output[i] = expected_output[i-2];
            }
            expected_output[1] = 0;
            expected_output[0] = 0;
        }

        state.waitForSync();
        initSharedMem(state);
        state.write().dsp_configuration->mixer1_enabled_dirty = true;
        state.write().dsp_configuration->mixer1_enabled = true;
        state.write().source_configurations->config[0].gain[1][0] = 1.0;
        state.write().source_configurations->config[0].gain_1_dirty = true;
        state.notifyDsp();
        printf("init\n");

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

            state.write().source_configurations->config[0].simple_filter.b0 = simple_b0;
            state.write().source_configurations->config[0].simple_filter.a1 = simple_a1;
            state.write().source_configurations->config[0].simple_filter_enabled = true;
            state.write().source_configurations->config[0].biquad_filter_enabled = true;
            state.write().source_configurations->config[0].biquad_filter.b0 = biquad_b0;
            state.write().source_configurations->config[0].biquad_filter.b1 = biquad_b1;
            state.write().source_configurations->config[0].biquad_filter.b2 = biquad_b2;
            state.write().source_configurations->config[0].biquad_filter.a1 = biquad_a1;
            state.write().source_configurations->config[0].biquad_filter.a2 = biquad_a2;
            state.write().source_configurations->config[0].filters_enabled_dirty = true;
            state.write().source_configurations->config[0].biquad_filter_dirty = true;
            state.write().source_configurations->config[0].simple_filter_dirty = true;

            state.notifyDsp();

            bool continue_reading = true;
            for (size_t frame_count = 0; continue_reading && frame_count < 10; frame_count++) {
                state.waitForSync();

                for (size_t i = 0; i < 160; i++) {
                    if (state.write().intermediate_mix_samples->mix1.pcm32[0][i]) {
                        printf("[intermediate] frame=%i, sample=%i\n", frame_count, i);
                        for (size_t j = 0; j < 60; j++) {
                            printf("%08lx ", (u32)state.write().intermediate_mix_samples->mix1.pcm32[0][j]);
                        }
                        continue_reading = false;
                        printf("\n");
                        break;
                    }
                }

                state.notifyDsp();
            }

            printf("expect:\n");
            for (size_t j = 0; j < 60; j++) {
                printf("%08lx ", (u32)expected_output[j]);
            }

            printf("Done!\n");
        }
    }

end:
    waitForKey();
    audioExit(state);
    gfxExit();
    return 0;
}

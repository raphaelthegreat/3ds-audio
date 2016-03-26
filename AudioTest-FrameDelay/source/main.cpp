#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <3ds.h>

#include "audio.h"

// High frequency square wave, PCM16
void fillBuffer(u32 *audio_buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        u32 data;
        switch (i % 5) {
        case 0:
            data = 0x1000;
            break;
        case 1:
            data = 0x6000;
            break;
        case 2:
            data = 0x4000;
            break;
        case 3:
            data = 0x2000;
            break;
        case 4:
            data = 0x5000;
            break;
        }
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

    state.waitForSync();
    initSharedMem(state);
    state.write().dsp_configuration->mixer1_enabled_dirty = true;
    state.write().dsp_configuration->mixer1_enabled = true;
    //state.write().dsp_configuration->limiter_enabled = 1;
    state.notifyDsp();
    printf("init\n");

    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    for (auto& gain : state.write().source_configurations->config[0].gain) {
        for (auto& g : gain) {
            g = 0.0;
        }
    }
    state.write().source_configurations->config[0].gain[0][0] = 1.0;
    state.write().source_configurations->config[0].gain[1][0] = 1.0;
    state.write().source_configurations->config[0].gain[1][1] = 0.5;
    state.write().source_configurations->config[0].gain_1_dirty = true;
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();

    {
        while (true) {
            state.waitForSync();
            printf("sync = %i, play = %i, cbi = %i\n", state.read().source_statuses->status[0].sync, state.read().source_statuses->status[0].is_enabled, state.read().source_statuses->status[0].current_buffer_id);
            if (state.read().source_statuses->status[0].sync == 1) break;
            state.notifyDsp();
        }
        printf("fi: %i\n", state.frame_id);

        u16 buffer_id = 0;
        //unsigned next_queue_position = 0;

        state.write().source_configurations->config[0].play_position = 0;
        state.write().source_configurations->config[0].physical_address = osConvertVirtToPhys(audio_buffer);
        state.write().source_configurations->config[0].length = NUM_SAMPLES;
        state.write().source_configurations->config[0].mono_or_stereo = DSP::HLE::SourceConfiguration::Configuration::MonoOrStereo::Stereo;
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

        state.notifyDsp();

        bool continue_reading = true;
        for (size_t frame_count = 0; continue_reading; frame_count++) {
            state.waitForSync();

            if (state.read().source_statuses->status[0].current_buffer_id) {
                printf("%i cbi = %i\n", frame_count, state.read().source_statuses->status[0].current_buffer_id);
            }

            for (size_t i = 0; i < 160; i++) {
                if (state.write().intermediate_mix_samples->mix1.pcm32[0][i]) {
                    printf("[intermediate] frame=%i, sample=%i\n", frame_count, i);
                    for (size_t j = 0; j < 20; j++) {
                        printf("%08lx ", (u32)state.write().intermediate_mix_samples->mix1.pcm32[0][j]);
                    }
                    printf("\n");
                    break;
                }
            }

            for (size_t i = 0; i < 160 * 2; i++) {
                if (state.read().final_samples->pcm16[i]) {
                    printf("[final] frame=%i, sample=%i\n", frame_count, i);
                    for (size_t j = 0; j < 20; j++) {
                        printf("%04x ", (u16)state.read().final_samples->pcm16[j]);
                    }
                    printf("\n");
                    continue_reading = false;
                    break;
                }
            }

            state.notifyDsp();
        }

        state.waitForSync();
        state.write().source_configurations->config[0].sync = 2;
        state.write().source_configurations->config[0].sync_dirty = true;
        state.notifyDsp();

        while (true) {
            state.waitForSync();
            printf("sync = %i, play = %i\n", state.read().source_statuses->status[0].sync, state.read().source_statuses->status[0].is_enabled);
            if (state.read().source_statuses->status[0].sync == 2) break;
            state.notifyDsp();
        }
        state.notifyDsp();

        printf("Done!\n");
    }

end:
    audioExit(state);
    waitForKey();
    gfxExit();
    return 0;
}

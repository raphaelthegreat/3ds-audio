#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cfenv>

#include <3ds.h>

#include "audio.h"

// Pseudorandom number generator
u16 prand() {
    static u16 lfsr = 0xACE1;
    u16 lsb = lfsr & 1;
    lfsr >>= 1;
    lfsr ^= (-lsb) & 0xB400u;
    return lfsr;
}

void fillBuffer(s16 *audio_buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        audio_buffer[i] = prand();
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

void do_test(FILE* file, AudioState& state, s16* audio_buffer, float rate_multiplier) {
    printf("do_test rate_multiplier = %f\n", rate_multiplier);
    fprintf(file, "rate_multiplier = %f\n", rate_multiplier);

    state.waitForSync();
    initSharedMem(state);
    state.write().dsp_configuration->mixer1_enabled_dirty = true;
    state.write().dsp_configuration->mixer1_enabled = true;
    state.write().source_configurations->config[0].gain[1][0] = 1.0;
    state.write().source_configurations->config[0].gain_1_dirty = true;
    state.notifyDsp();

    {
        u16 buffer_id = 0;

        state.waitForSync();

        state.write().source_configurations->config[0].play_position = 0;
        state.write().source_configurations->config[0].physical_address = osConvertVirtToPhys(audio_buffer);
        state.write().source_configurations->config[0].length = 160;
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
        state.write().source_configurations->config[0].interpolation_mode = DSP::HLE::SourceConfiguration::Configuration::InterpolationMode::Linear;
        state.write().source_configurations->config[0].interpolation_related = 0;
        state.write().source_configurations->config[0].interpolation_dirty = true;

        state.notifyDsp();

        bool continue_reading = true;
        for (size_t frame_count = 0; continue_reading && frame_count < 10; frame_count++) {
            state.waitForSync();

            for (size_t i = 0; i < 160; i++) {
                if (state.write().intermediate_mix_samples->mix1.pcm32[0][i]) {
                    for (size_t j = 0; j < 160; j++) {
                        fprintf(file, "[%03i] = %04hx\n", j, static_cast<u16>(state.read().intermediate_mix_samples->mix1.pcm32[0][j]));
                    }
                    continue_reading = false;
                    fprintf(file, "\n");
                    break;
                }
            }

            state.notifyDsp();
        }
    }
}

int main(int argc, char **argv) {
    gfxInitDefault();

    PrintConsole topScreen;
    consoleInit(GFX_TOP, &topScreen);
    consoleSelect(&topScreen);

    constexpr size_t NUM_SAMPLES = 160;
    s16* audio_buffer = (s16*) linearAlloc(NUM_SAMPLES * sizeof(s16) * 2);
    fillBuffer(audio_buffer, NUM_SAMPLES * 2);

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
        FILE* file = fopen("sdmc:/AudioTest-InterpLinear-ToFile.log.txt", "w");

        do_test(file, state, audio_buffer, 0.4f);
        do_test(file, state, audio_buffer, 3.0f);
        do_test(file, state, audio_buffer, 31.f/127.f);
        do_test(file, state, audio_buffer, 1.0f);
        do_test(file, state, audio_buffer, 0.1237f);

        fclose(file);
    }

    printf("Done! (press a key)");

end:
    audioExit(state);
    waitForKey();
    gfxExit();
    return 0;
}

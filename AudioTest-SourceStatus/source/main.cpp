#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <3ds.h>

#include "audio.h"

// World's worst triangle wave generator.
// Generates PCM16.
void fillBuffer(u32 *audio_buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        u32 data = (i % 160) * 256;
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

    printf("WARNING: This test doesn't handle DSP sleep so don't close your console or you'll have to hard reboot your console\n\n");

    constexpr size_t NUM_SAMPLES = 160*100;
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
    state.notifyDsp();
    printf("init\n");

    {
        for (size_t frame_count = 0; frame_count < 20; frame_count++) {
            state.waitForSync();
            state.notifyDsp();
        }


        u16 buffer_id = 0;
        size_t next_queue_position = 0;

        state.write().source_configurations->config[0].play_position = 0;
        state.write().source_configurations->config[0].physical_address = osConvertVirtToPhys(audio_buffer);
        state.write().source_configurations->config[0].length = NUM_SAMPLES;
        state.write().source_configurations->config[0].mono_or_stereo = DSP::HLE::SourceConfiguration::Configuration::MonoOrStereo::Stereo;
        state.write().source_configurations->config[0].format = DSP::HLE::SourceConfiguration::Configuration::Format::PCM16;
        state.write().source_configurations->config[0].fade_in = false;
        state.write().source_configurations->config[0].adpcm_dirty = false;
        state.write().source_configurations->config[0].is_looping = false;
        state.write().source_configurations->config[0].buffer_id = buffer_id;
        state.write().source_configurations->config[0].reset_flag = true;
        state.write().source_configurations->config[0].use_queue_flag = true;
        state.write().source_configurations->config[0].embedded_buffer_dirty = true;

        state.write().source_configurations->config[0].buffers[next_queue_position].physical_address = osConvertVirtToPhys(audio_buffer);
        state.write().source_configurations->config[0].buffers[next_queue_position].length = NUM_SAMPLES;
        state.write().source_configurations->config[0].buffers[next_queue_position].adpcm_dirty = false;
        state.write().source_configurations->config[0].buffers[next_queue_position].is_looping = false;
        state.write().source_configurations->config[0].buffers[next_queue_position].buffer_id = ++buffer_id;
        state.write().source_configurations->config[0].buffers_dirty |= 1 << next_queue_position;
        next_queue_position = (next_queue_position + 1) % 4;
        state.write().source_configurations->config[0].buffer_queue_dirty = true;

        state.write().source_configurations->config[0].enable = true;
        state.write().source_configurations->config[0].enable_dirty = true;

        for (size_t frame_count = 0; frame_count < 950; frame_count++) {
            state.waitForSync();

            if (!state.read().source_statuses->status[0].is_enabled) {
                printf("%i !\n", frame_count);
                state.write().source_configurations->config[0].enable = true;
                state.write().source_configurations->config[0].enable_dirty = true;
            }

            if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                printf("%i %i\n", frame_count, state.read().source_statuses->status[0].current_buffer_id);
                if (state.read().source_statuses->status[0].current_buffer_id == buffer_id || state.read().source_statuses->status[0].current_buffer_id == 0) {
                    state.write().source_configurations->config[0].buffers[next_queue_position].physical_address = osConvertVirtToPhys(audio_buffer);
                    state.write().source_configurations->config[0].buffers[next_queue_position].length = NUM_SAMPLES;
                    state.write().source_configurations->config[0].buffers[next_queue_position].adpcm_dirty = false;
                    state.write().source_configurations->config[0].buffers[next_queue_position].is_looping = false;
                    state.write().source_configurations->config[0].buffers[next_queue_position].buffer_id = ++buffer_id;
                    state.write().source_configurations->config[0].buffers_dirty |= 1 << next_queue_position;
                    next_queue_position = (next_queue_position + 1) % 4;
                    state.write().source_configurations->config[0].buffer_queue_dirty = true;
                }
            }

            state.notifyDsp();
        }

        u16 prev_read_bid = state.read().source_statuses->status[0].current_buffer_id;
        for (size_t frame_count = 950; frame_count < 1108; frame_count++) {
            state.waitForSync();

            if (!state.read().source_statuses->status[0].is_enabled) {
                printf("%i !\n", frame_count);
            }

            if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                printf("%i d\n", frame_count);
            }

            if (prev_read_bid != state.read().source_statuses->status[0].current_buffer_id) {
                printf("%i %i\n", frame_count, state.read().source_statuses->status[0].current_buffer_id);
                prev_read_bid = state.read().source_statuses->status[0].current_buffer_id;
            }

            state.notifyDsp();
        }

        printf("Done!\n");
    }

end:
    waitForKey();
    audioExit(state);
    gfxExit();
    return 0;
}

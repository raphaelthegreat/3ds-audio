#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <3ds.h>

#include "audio.h"

// World's worst triangle wave generator.
// Generates PCM16.
void fillBuffer(u32 *audio_buffer, size_t size, unsigned freq) {
    for (size_t i = 0; i < size; i++) {
        u32 data = (i % freq) * 256;
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
    fillBuffer(audio_buffer, NUM_SAMPLES, 160);
    u32 *audio_buffer2 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer2, NUM_SAMPLES, 80);
    u32 *audio_buffer3 = (u32*)linearAlloc(NUM_SAMPLES * sizeof(u32));
    fillBuffer(audio_buffer3, NUM_SAMPLES, 40);

    unsigned num_channels;

    printf("A: num_channels = 0\n");
    printf("B: num_channels = 1\n");
    printf("X: num_channels = 2\n");
    printf("Y: num_channels = 3\n");

    while (aptMainLoop()) {
        gfxSwapBuffers();
        gfxFlushBuffers();
        gspWaitForVBlank();

        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_A) {
            num_channels = 0;
            break;
        }
        if (kDown & KEY_B) {
            num_channels = 1;
            break;
        }
        if (kDown & KEY_X) {
            num_channels = 2;
            break;
        }
        if (kDown & KEY_Y) {
            num_channels = 3;
            break;
        }
    }

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

    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();
    state.waitForSync();
    state.notifyDsp();

    {
        while (true) {
            state.waitForSync();
            printf("sync = %i, play = %i\n", state.read().source_statuses->status[0].sync, state.read().source_statuses->status[0].is_enabled);
            if (state.read().source_statuses->status[0].sync == 1) break;
            state.notifyDsp();
        }
        printf("fi: %i\n", state.frame_id);

        u16 buffer_id = 0;
        size_t next_queue_position = 0;

        state.write().source_configurations->config[0].play_position = 0;
        state.write().source_configurations->config[0].physical_address = osConvertVirtToPhys(audio_buffer3);
        state.write().source_configurations->config[0].length = NUM_SAMPLES;
        state.write().source_configurations->config[0].mono_or_stereo = DSP::HLE::SourceConfiguration::Configuration::MonoOrStereo(num_channels);
        state.write().source_configurations->config[0].format = DSP::HLE::SourceConfiguration::Configuration::Format::PCM16;
        state.write().source_configurations->config[0].fade_in = false;
        state.write().source_configurations->config[0].adpcm_dirty = false;
        state.write().source_configurations->config[0].is_looping = false;
        state.write().source_configurations->config[0].buffer_id = ++buffer_id;
        state.write().source_configurations->config[0].partial_reset_flag = true;
        state.write().source_configurations->config[0].play_position_dirty = true;
        state.write().source_configurations->config[0].embedded_buffer_dirty = true;

        state.write().source_configurations->config[0].buffers[next_queue_position].physical_address = osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
        state.write().source_configurations->config[0].buffers[next_queue_position].length = NUM_SAMPLES;
        state.write().source_configurations->config[0].buffers[next_queue_position].adpcm_dirty = false;
        state.write().source_configurations->config[0].buffers[next_queue_position].is_looping = false;
        state.write().source_configurations->config[0].buffers[next_queue_position].buffer_id = ++buffer_id;
        state.write().source_configurations->config[0].buffers_dirty |= 1 << next_queue_position;
        next_queue_position = (next_queue_position + 1) % 4;
        state.write().source_configurations->config[0].buffer_queue_dirty = true;
        state.write().source_configurations->config[0].enable = true;
        state.write().source_configurations->config[0].enable_dirty = true;

        state.notifyDsp();

        for (size_t frame_count = 0; frame_count < 1950; frame_count++) {
            state.waitForSync();

            if (!state.read().source_statuses->status[0].is_enabled) {
                printf("%i !\n", frame_count);
                state.write().source_configurations->config[0].enable = true;
                state.write().source_configurations->config[0].enable_dirty = true;
            }

            if (state.read().source_statuses->status[0].current_buffer_id_dirty) {
                printf("%i %i (curr:%i)\n", frame_count, state.read().source_statuses->status[0].current_buffer_id, buffer_id+1);
                if (state.read().source_statuses->status[0].current_buffer_id == buffer_id || state.read().source_statuses->status[0].current_buffer_id == 0) {
                    state.write().source_configurations->config[0].buffers[next_queue_position].physical_address = osConvertVirtToPhys(buffer_id % 2 ? audio_buffer2 : audio_buffer);
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
        for (size_t frame_count = 1950; frame_count < 2208; frame_count++) {
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

        printf("last buf id %i\n", buffer_id);

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
    waitForKey();
    audioExit(state);
    gfxExit();
    return 0;
}

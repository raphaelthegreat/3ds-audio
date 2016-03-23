#include <array>
#include <experimental/optional>
#include <vector>

#include <3ds.h>

#include "dsp.h"

using namespace std;
using namespace std::experimental;

struct SharedMem {
    u16* frame_counter;

    DSP::HLE::SourceConfiguration* source_configurations;
    DSP::HLE::SourceStatus* source_statuses;
    DSP::HLE::AdpcmCoefficients* adpcm_coefficients;

    DSP::HLE::DspConfiguration* dsp_configuration;
    DSP::HLE::DspStatus* dsp_status;

    DSP::HLE::FinalMixSamples* final_samples;
    DSP::HLE::IntermediateMixSamples* intermediate_mix_samples;

    DSP::HLE::Compressor* compressor;

    DSP::HLE::DspDebug* dsp_debug;
};

struct AudioState {
    Handle pipe2_irq = 0;
    Handle dsp_semaphore = 0;

    array<array<u16*, 2>, 16> dsp_structs;
    array<SharedMem, 2> shared_mem;
    u16 frame_id = 2;

    const SharedMem& read() const;
    const SharedMem& write() const;
    void waitForSync();
    void notifyDsp();
};

optional<vector<u8>> loadDspFirmFromFile();
optional<AudioState> audioInit(vector<u8> dspfirm);
void audioExit(const AudioState& state);
void initSharedMem(AudioState& state);

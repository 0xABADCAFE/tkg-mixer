#include <stdio.h>

#include "mixer.h"
#include <proto/exec.h>

#include <devices/timer.h>
#include <proto/timer.h>

typedef long long LONG64;
typedef unsigned long long ULONG64;

typedef union {
    struct EClockVal ecv;
    ULONG64 ticks;
} ClockValue;

extern struct ExecBase * SysBase;

ULONG clock_freq_hz = 0;

ClockValue clk_begin, clk_end;

struct TimeRequest time_request;
struct Device* TimerBase = NULL;

static BOOL check_cpu(void) {
    return 0 != (SysBase->AttnFlags & (AFF_68040|AFF_68060));
}

struct Device* get_timer(void) {
    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, &time_request.tr_node, 0) != 0) {
        return NULL;
    }
    TimerBase = time_request.tr_node.io_Device;
    clock_freq_hz = ReadEClock(&clk_begin.ecv);
    printf("Got Timer, frequency is %u Hz\n", clock_freq_hz);
    return TimerBase;
}

void free_timer(void) {
    if (TimerBase) {
        CloseDevice(&time_request.tr_node);
        TimerBase = NULL;
    }
}

extern UWORD asm_sizeof_mixer;

int main(void) {
    if (!check_cpu()) {
        puts("CPU Check failed. 68040 or 68060 is required");
        return 10;
    }

    if (sizeof(Aud_Mixer) != asm_sizeof_mixer) {
        printf(
            "Unexpected structure size difference, C: %zu, asm: %zu\n",
            sizeof(Aud_Mixer),
            (size_t)asm_sizeof_mixer
        );
        return 20;
    }

    Aud_Mixer* mixer = Aud_CreateMixer(16000, 50);

    if (mixer) {

        BYTE* sample = AllocCacheAligned(256, MEMF_FAST);
        if (sample) {
            BYTE* p = sample;
            for (int i = -128; i < 128; ++i) {
                *p++ = (BYTE)i;
            }
            mixer->am_ChannelState[1].ac_SamplePtr   = sample;
            mixer->am_ChannelState[1].ac_SamplesLeft = 256;
            mixer->am_ChannelState[1].ac_LeftVolume  = 15;
            mixer->am_ChannelState[1].ac_RightVolume = 7;

            mixer->am_ChannelState[5].ac_SamplePtr   = sample+128;
            mixer->am_ChannelState[5].ac_SamplesLeft = 32;
            mixer->am_ChannelState[5].ac_LeftVolume  = 7;
            mixer->am_ChannelState[5].ac_RightVolume = 15;
            Aud_DumpMixer(mixer);

            for (int j = 0; j < 4; ++j) {
                Aud_MixLine(mixer);
                Aud_DumpMixer(mixer);
            }

            FreeCacheAligned(sample);
        }
        Aud_FreeMixer(mixer);
    }

    return 0;
}

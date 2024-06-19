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

typedef struct {
    BYTE* s_dataPtr;
    ULONG s_length;
} Sound;

void load_sample(char const* file_name, Sound* sound)
{
    FILE *file = fopen(file_name, "rb");
    if (file) {
        fseek(file, 0, SEEK_END);
        size_t size = ftell(file) & 0xFFFF;
        fseek(file, 0, SEEK_SET);

        BYTE* alloc =  AllocCacheAligned(size, MEMF_FAST);
        if (alloc) {
            fread(alloc, 1, size, file);
            printf("Loaded %s [%zu bytes] at %p\n", file_name, size, alloc);
            sound->s_dataPtr = alloc;
            sound->s_length  = CacheAlign(size);
        }
        fclose(file);
    }
}

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
        TimerBase = get_timer();

        // Just have a few sounds to play
        Sound sound[3] = {};

        load_sample("sounds/Teleport.raw", &sound[0]);
        load_sample("sounds/RumbleWind.raw", &sound[1]);
        load_sample("sounds/Collect_weapon.raw", &sound[2]);

        mixer->am_ChannelState[2].ac_SamplePtr   = sound[0].s_dataPtr;
        mixer->am_ChannelState[2].ac_SamplesLeft = sound[0].s_length;
        mixer->am_ChannelState[2].ac_LeftVolume  = 15;
        mixer->am_ChannelState[2].ac_RightVolume = 5;

        mixer->am_ChannelState[3].ac_SamplePtr   = sound[1].s_dataPtr;
        mixer->am_ChannelState[3].ac_SamplesLeft = sound[1].s_length;
        mixer->am_ChannelState[3].ac_LeftVolume  = 1;
        mixer->am_ChannelState[3].ac_RightVolume = 1;

        mixer->am_ChannelState[5].ac_SamplePtr   = sound[2].s_dataPtr;
        mixer->am_ChannelState[5].ac_SamplesLeft = sound[2].s_length;
        mixer->am_ChannelState[5].ac_LeftVolume  = 0;
        mixer->am_ChannelState[5].ac_RightVolume = 8;

        Aud_DumpMixer(mixer);


        FILE* lchan_out = fopen("lchan_out.raw", "wb");
        FILE* rchan_out = fopen("rchan_out.raw", "wb");
        FILE* lvol_out  = fopen("lvol_out.raw", "wb");
        FILE* rvol_out  = fopen("rvol_out.raw", "wb");

        ULONG ticks;
        ULONG packets = 0;
        while (mixer->am_ChannelState[3].ac_SamplesLeft > 0) {

            ReadEClock(&clk_begin.ecv);
            Aud_MixPacket(mixer);
            ReadEClock(&clk_end.ecv);
            ++packets;
            ticks += (ULONG)(clk_end.ticks - clk_begin.ticks);

            fwrite(mixer->am_LeftPacketSampleBasePtr, 1, mixer->am_PacketSize, lchan_out);
            fwrite(mixer->am_RightPacketSampleBasePtr, 1, mixer->am_PacketSize, rchan_out);

            fwrite(mixer->am_LeftPacketVolumeBasePtr, 2, mixer->am_PacketSize >> 4, lvol_out);
            fwrite(mixer->am_RightPacketVolumeBasePtr, 2, mixer->am_PacketSize >> 4, rvol_out);
        }

        fclose(lchan_out);
        fclose(lvol_out);
        fclose(rchan_out);
        fclose(rvol_out);

        Aud_DumpMixer(mixer);

        FreeCacheAligned(sound[0].s_dataPtr);
        FreeCacheAligned(sound[1].s_dataPtr);
        FreeCacheAligned(sound[2].s_dataPtr);

        printf("Mixed %lu Packets in %lu EClockVal ticks (%lu/s)\n", packets, ticks, clock_freq_hz);

        free_timer();
        Aud_FreeMixer(mixer);
    }

    return 0;
}

#include <stdio.h>

#include "mixer.h"
#include <proto/exec.h>
#include <devices/timer.h>
#include <proto/timer.h>

#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/dos.h>

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

#define OPT_USE_060 0
#define OPT_DUMP_BUFFERS 1

LONG run_params[2] = { 0, 0 };

int main(void) {
    if (!check_cpu()) {
        puts("CPU Check failed. 68040 or 68060 is required");
        return 10;
    }

    struct RDArgs* args = NULL;

    if ( (args = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL) )) {

        if (ReadArgs("Use060/S,D=DumpBuffers/S", run_params, args)) {
            FreeArgs(args);
        }
        FreeDosObject(DOS_RDARGS, args);
        args = NULL;
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

        Sound sound;
        Sound inverse;

        load_sample("sounds/airstrike.raw", &sound);

        inverse.s_dataPtr = AllocCacheAligned(sound.s_length, MEMF_FAST);
        for (int s = 0; s < sound.s_length; ++s) {
            inverse.s_dataPtr[s] = - sound.s_dataPtr[s];
        }

        for (int chan = 0; chan < AUD_NUM_CHANNELS; ++chan) {
            mixer->am_ChannelState[chan].ac_SamplePtr   = (
                (chan & 1) ? sound.s_dataPtr : inverse.s_dataPtr
            ) + (chan << 5);
            mixer->am_ChannelState[chan].ac_SamplesLeft = sound.s_length - (chan << 5);
            mixer->am_ChannelState[chan].ac_LeftVolume  = chan;
            mixer->am_ChannelState[chan].ac_RightVolume = 15 - chan;
        }

        Aud_DumpMixer(mixer);

        FILE* lchan_out = NULL;
        FILE* rchan_out = NULL;
        FILE* lvol_out  = NULL;
        FILE* rvol_out  = NULL;

        if (run_params[OPT_DUMP_BUFFERS]) {
            lchan_out = fopen("lchan_out.raw", "wb");
            rchan_out = fopen("rchan_out.raw", "wb");
            lvol_out  = fopen("lvol_out.raw", "wb");
            rvol_out  = fopen("rvol_out.raw", "wb");
        }

        ULONG ticks;
        ULONG packets = 0;
        while (mixer->am_ChannelState[0].ac_SamplesLeft > 0) {

            ReadEClock(&clk_begin.ecv);
            Aud_MixPacket_040(mixer);
            ReadEClock(&clk_end.ecv);
            ++packets;
            ticks += (ULONG)(clk_end.ticks - clk_begin.ticks);

            if (lchan_out) {
                fwrite(mixer->am_LeftPacketSampleBasePtr, 1, mixer->am_PacketSize, lchan_out);
            }

            if (rchan_out) {
                fwrite(mixer->am_RightPacketSampleBasePtr, 1, mixer->am_PacketSize, rchan_out);
            }

            if (lvol_out) {
                fwrite(mixer->am_LeftPacketVolumeBasePtr, 2, mixer->am_PacketSize >> 4, lvol_out);
            }

            if (rvol_out) {
                fwrite(mixer->am_RightPacketVolumeBasePtr, 2, mixer->am_PacketSize >> 4, rvol_out);
            }
        }

        if (lchan_out) {
            fclose(lchan_out);
        }

        if (lvol_out) {
            fclose(lvol_out);
        }

        if (rchan_out) {
            fclose(rchan_out);
        }

        if (rvol_out) {
            fclose(rvol_out);
        }
        Aud_DumpMixer(mixer);

        FreeCacheAligned(sound.s_dataPtr);
        FreeCacheAligned(inverse.s_dataPtr);

        printf("Mixed %lu Packets in %lu EClockVal ticks (%lu/s)\n", packets, ticks, clock_freq_hz);

        free_timer();
        Aud_FreeMixer(mixer);
    }

    return 0;
}

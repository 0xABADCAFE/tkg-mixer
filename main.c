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

enum {
    OPT_USE_060 = 0,
    OPT_LINEAR,
    OPT_DUMP_BUFFERS,
    OPT_VERBOSE,
    OPT_MAX
};

static LONG ra_Params[OPT_MAX] = { 0, 0, 0, 0 };

static void parse_params(void) {
    struct RDArgs* args = NULL;
    if ( (args = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL) )) {
        if (ReadArgs("6=USE060/S,L=LINEAR/S,D=DUMPBUFFERS/S,V=VERBOSE/S", ra_Params, args)) {
            FreeArgs(args);
        }
        FreeDosObject(DOS_RDARGS, args);
        args = NULL;

        if (ra_Params[OPT_USE_060]) {
            puts("Using 68060 code path");
        } else {

            if (ra_Params[OPT_LINEAR]) {
                puts("Using 68040 linear lookup code path");
            } else {
                puts("Using 68040 delta lookup code path");
            }

        }
    }
}

static FILE* db_LChanOut = NULL;
static FILE* db_RChanOut = NULL;
static FILE* db_LVolOut  = NULL;
static FILE* db_RVolOut  = NULL;

static void open_dump(void) {
    db_LChanOut = fopen("lchan_out.raw", "wb");
    db_RChanOut = fopen("rchan_out.raw", "wb");
    db_LVolOut  = fopen("lvol_out.raw", "wb");
    db_RVolOut  = fopen("rvol_out.raw", "wb");
}

static void close_dump(void) {
    if (db_LChanOut) {
        fclose(db_LChanOut);
    }
    if (db_LVolOut) {
        fclose(db_LVolOut);
    }
    if (db_RChanOut) {
        fclose(db_RChanOut);
    }
    if (db_RVolOut) {
        fclose(db_RVolOut);
    }
}

void dump_mixer(Aud_Mixer const* mixer) {
    if (db_LChanOut) {
        fwrite(mixer->am_LeftPacketSampleBasePtr, 1, mixer->am_PacketSize, db_LChanOut);
    }
    if (db_RChanOut) {
        fwrite(mixer->am_RightPacketSampleBasePtr, 1, mixer->am_PacketSize, db_RChanOut);
    }
    if (db_LVolOut) {
        fwrite(mixer->am_LeftPacketVolumeBasePtr, 2, mixer->am_PacketSize >> 4, db_LVolOut);
    }
    if (db_RVolOut) {
        fwrite(mixer->am_RightPacketVolumeBasePtr, 2, mixer->am_PacketSize >> 4, db_RVolOut);
    }
}

#define time(f) \
    ReadEClock(&clk_begin.ecv);\
    f; \
    ReadEClock(&clk_end.ecv);


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

    parse_params();

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

        if (ra_Params[OPT_DUMP_BUFFERS]) {
            open_dump();
        }

        for (int max_chan = 1; max_chan <= AUD_NUM_CHANNELS; ++max_chan) {
            printf("Testing with %d channel(s)\n", max_chan);

            for (int chan = 0; chan < max_chan; ++chan) {
                mixer->am_ChannelState[chan].ac_SamplePtr   = (
                    (chan & 1) ? sound.s_dataPtr : inverse.s_dataPtr
                ) + (chan << 5);
                mixer->am_ChannelState[chan].ac_SamplesLeft = sound.s_length - (chan << 5);
                mixer->am_ChannelState[chan].ac_LeftVolume  = chan;
                mixer->am_ChannelState[chan].ac_RightVolume = 15 - chan;
            }

            if (ra_Params[OPT_VERBOSE]) {
                Aud_DumpMixer(mixer);
            }

            ULONG ticks   = 0;
            ULONG packets = 0;

            while (mixer->am_ChannelState[0].ac_SamplesLeft > 0) {
                if (ra_Params[OPT_USE_060]) {
                    time(Aud_MixPacket_060(mixer));
                }
                else if (ra_Params[OPT_LINEAR]) {
                    time(Aud_MixPacket_040Linear(mixer));
                } else {
                    time(Aud_MixPacket_040Delta(mixer));
                }
                ++packets;
                ticks += (ULONG)(clk_end.ticks - clk_begin.ticks);

                if (ra_Params[OPT_DUMP_BUFFERS]) {
                    dump_mixer(mixer);
                }
            }
            if (ra_Params[OPT_VERBOSE]) {
                Aud_DumpMixer(mixer);
            }
            printf("Mixed %lu Packets in %lu EClockVal ticks (%lu/s)\n", packets, ticks, clock_freq_hz);
        }

        if (ra_Params[OPT_DUMP_BUFFERS]) {
            close_dump();
        }


        FreeCacheAligned(sound.s_dataPtr);
        FreeCacheAligned(inverse.s_dataPtr);

        free_timer();
        Aud_FreeMixer(mixer);
    }

    return 0;
}

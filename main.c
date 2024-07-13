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
    printf("Got Timer, tick frequency is %u Hz\n", clock_freq_hz);
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
    OPT_DUMP_BUFFERS=0,
    OPT_VERBOSE,
    OPT_MAX
};

static LONG ra_Params[OPT_MAX] = { 0, 0, };

static void parse_params(void) {
    struct RDArgs* args = NULL;
    if ( (args = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL) )) {
        if (ReadArgs("D=DUMPBUFFERS/S,V=VERBOSE/S", ra_Params, args)) {
            FreeArgs(args);
        }
        FreeDosObject(DOS_RDARGS, args);
        args = NULL;
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


typedef void (*Mix_Function)(REG(a0, Aud_Mixer* mixer));

typedef struct {
    Mix_Function mix_function;
    char const*  mix_info;
    char const*  norm_info;
    char const*  extra_info;
} TestCase;

static TestCase test_cases[] = {
    {
        Aud_MixPacket_040Null,
        "None (data fectch only)",
        "None (data write only)",
        "Move16 fetch, target 68040/60"
    },

    {
        Aud_MixPacket_060,
        "Multiplication",
        "Multiplication/Shift",
        "Move16 fetch, target 68060"
    },

    {
        Aud_MixPacket_040Shifted,
        "Shift Only",
        "Multiplication/Shift",
        "Move16 fetch, target 68040"
    },

    {
        Aud_MixPacket_040Linear,
        "Lookup",
        "Multiplication/Shift",
        "Move16 fetch, target 68040/60"
    },
    {
        Aud_MixPacket_040Delta,
        "Delta Lookup",
        "Multiplication/Shift",
        "Move16 fetch, target 68040/60"
    },

    // Pre-encoded tests follow. The samples will be converted
    {
        Aud_MixPacket_040PreDelta,
        "Delta Lookup (Pre-encoded source)",
        "Multiplication/Shift",
        "Move16 fetch, target 68040"
    },

};


void EncodeL1D15(Sound* p_sound) {
    ULONG frames = p_sound->s_length >> 4;
    BYTE *p_data = p_sound->s_dataPtr;

    for (ULONG f = 0; f<frames; ++f) {

        for (int i=15; i > 0; --i) {
            p_data[i] -= p_data[i - 1];
        }

        p_data += CACHE_LINE_SIZE;
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

        for (size_t test = 0; test < sizeof(test_cases)/sizeof(TestCase); ++test) {

            if (Aud_MixPacket_040PreDelta == test_cases->mix_function) {
                EncodeL1D15(&sound);
                EncodeL1D15(&inverse);
            }

            printf(
                "Test case %zu:\n"
                "\tMix : %s\n"
                "\tNorm: %s\n"
                "\tInfo: %s\n\n",
                test,
                test_cases[test].mix_info,
                test_cases[test].norm_info,
                test_cases[test].extra_info
            );

            for (int max_chan = 1; max_chan <= AUD_NUM_CHANNELS; ++max_chan) {
                printf("\tMixing %2d channel(s): ", max_chan);

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
                    time(test_cases[test].mix_function(mixer));

                    ++packets;
                    ticks += (ULONG)(clk_end.ticks - clk_begin.ticks);

                    if (ra_Params[OPT_DUMP_BUFFERS]) {
                        dump_mixer(mixer);
                    }
                }
                if (ra_Params[OPT_VERBOSE]) {
                    Aud_DumpMixer(mixer);
                }
                printf(" %7lu ticks %lu packets\n", ticks, packets);
            }

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

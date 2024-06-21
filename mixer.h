#ifndef _TKG_MIXER_H_
#define _TKG_MIXER_H_

#include <SDI_compiler.h>
#include <exec/types.h>

// CPU cache line size
#define CACHE_LINE_SIZE 16
#define CACHE_ALIGN_MASK (CACHE_LINE_SIZE - 1)

// Number of levels when converting 8 bit to 16 for a given volume level
#define AUD_8_TO_16_LEVELS 16

#define AUD_NUM_CHANNELS 16

#define MIN_SAMPLE_RATE 8000
#define MAX_SAMPLE_RATE 22050
#define MIN_UPDATE_RATE 10
#define MAX_UPDATE_RATE 100

typedef struct {
    BYTE*   ac_SamplePtr;   // The current sample address, or NULL
    UWORD   ac_SamplesLeft; // Number of unplayed samples remaining
    UBYTE   ac_LeftVolume;
    UBYTE   ac_RightVolume;
} Aud_ChannelState;

typedef struct {
    Aud_ChannelState am_ChannelState[AUD_NUM_CHANNELS];

    // The am_FetchBuffer contains the set of 8-bit samples just fetched for the current channel
    BYTE am_FetchBuffer[CACHE_LINE_SIZE];

    // The am_AccumL/am_AccumR buffers contain the current 16-bit mixed data
    WORD am_AccumL[CACHE_LINE_SIZE];
    WORD am_AccumR[CACHE_LINE_SIZE];

    // Chip RAM Buffer Pointers (working)
    BYTE*  am_LeftPacketSamplePtr;  // contains am_PacketSize normalised 8-bit sample data for the left channel
    UWORD* am_LeftPacketVolumePtr;  // contains am_PacketSize/16 6-bit volume modulation data for the left channel
    BYTE*  am_RightPacketSamplePtr; // contains am_PacketSize normalised 8-bit sample data for the right channel
    UWORD* am_RightPacketVolumePtr; // contains am_PacketSize/16 6-bit volume modulation data for the right channel

    // Counters
    UWORD  am_AbsMaxL;
    UWORD  am_AbsMaxR;
    UWORD  am_IndexL;
    UWORD  am_IndexR;

    // For machines with fast multiplication, we don't need to use the per-sample lookup
    // volume tables, we can just store the scaling factors here.
    WORD   am_VolumeScale[AUD_8_TO_16_LEVELS];

    // Config
    BYTE*  am_LeftPacketSampleBasePtr;  // contains am_PacketSize normalised 8-bit sample data for the left channel
    UWORD* am_LeftPacketVolumeBasePtr;  // contains am_PacketSize/16 6-bit volume modulation data for the left channel
    BYTE*  am_RightPacketSampleBasePtr; // contains am_PacketSize normalised 8-bit sample data for the right channel
    UWORD* am_RightPacketVolumeBasePtr; // contains am_PacketSize/16 6-bit volume modulation data for the right

    UBYTE* am_ChipBufferPtr;

    UWORD  am_SampleRateHz;
    UWORD  am_UpdateRateHz;
    UWORD  am_PacketSize;
    UWORD  am_TableOffset;
    UBYTE  am_UseMultiplyMixing;
    UBYTE  am_UseMultiplyNormalisation;
} Aud_Mixer;

extern Aud_Mixer *Aud_CreateMixer(
    REG(d0, UWORD sampleRateHz),
    REG(d1, UWORD updateRateHz)
);

extern void Aud_FreeMixer(
    REG(a0, Aud_Mixer* mixer)
);

extern void Aud_SetMixerVolume(
    REG(a0, Aud_Mixer* mixer),
    REG(d0, UWORD volume)
);

extern void Aud_MixPacket(
    REG(a0, Aud_Mixer* mixer)
);

extern void Aud_DumpMixer(
    REG(a0, Aud_Mixer* mixer)
);

extern void Aud_ResetBuffers(
    REG(a0, Aud_Mixer* mixer)
);


extern void* AllocCacheAligned(REG(d0, ULONG size), REG(d1, ULONG flags));
extern void FreeCacheAligned(REG(a0, void* address));

static inline ULONG CacheAlign(ULONG size)
{
    return (size + CACHE_ALIGN_MASK) & ~CACHE_ALIGN_MASK;
}

#endif

#include "mixer.h"
#include <stdio.h>
#include <proto/exec.h>


#define CACHE_ALIGN_MASK (CACHE_LINE_SIZE - 1)

static inline ULONG CacheAlign(ULONG size)
{
    return (size + CACHE_ALIGN_MASK) & ~CACHE_ALIGN_MASK;
}

/**
 * Utility function for allocating a block of cache aligned memory.
 * Uses AllocVec, reserving 4-16 bytes before the returned aligned address. The offset to the
 * base address returned from AllocVec is in the word preceding the returned address.
 */
void* AllocCacheAligned(REG(d0, ULONG size), REG(d1, ULONG flags))
{
    size = CacheAlign(size + CACHE_LINE_SIZE);
    UBYTE*  address = (UBYTE*)AllocVec(size, flags);
    if (address) {
        size_t  align_adjustment = CACHE_LINE_SIZE - (((size_t)address) & CACHE_ALIGN_MASK);
        address += align_adjustment;
        *((size_t*)(address - sizeof(size_t))) = align_adjustment;
    }
    return address;
}

/**
 * Utility function for freeing a block of cache aligned memory
 */
void FreeCacheAligned(REG(a0, void* address))
{
    if (!address || CACHE_ALIGN_MASK & ((ULONG)address)) {
        return;
    }

    UBYTE* byte_address     = (UBYTE*)address;
    size_t align_adjustment = *((size_t*)(address - sizeof(size_t)));

    if (align_adjustment < sizeof(size_t) || align_adjustment > CACHE_LINE_SIZE) {
        return;
    }

    byte_address -= align_adjustment;
    FreeVec(byte_address);
}

static void ClearAligned(void* address, size_t size)
{
    ULONG* dst = (ULONG*)address;
    size >>= 2;
    while (size--) {
        *dst++ = 0;
    }
}



#include <stdio.h>

Aud_Mixer *Aud_CreateMixer(
    REG(d0, UWORD sampleRateHz),
    REG(d1, UWORD updateRateHz)
)
{
    if (
        sampleRateHz < MIN_SAMPLE_RATE ||
        sampleRateHz > MAX_SAMPLE_RATE ||
        updateRateHz < MIN_UPDATE_RATE ||
        updateRateHz > MAX_UPDATE_RATE
    ) {
        return NULL;
    }

    size_t context_size = CacheAlign(sizeof(Aud_Mixer));

    size_t tables_size  = (AUD_8_TO_16_LEVELS - 1) * 256 * sizeof(WORD);

    Aud_Mixer* mixer = AllocCacheAligned(context_size + tables_size, MEMF_ANY);
    if (mixer) {
        ClearAligned(mixer, context_size);

        mixer->am_LeftPacketSamplePtr = NULL;
        mixer->am_SampleRateHz = sampleRateHz;
        mixer->am_UpdateRateHz = updateRateHz;
        mixer->am_PacketSize   = (UWORD)CacheAlign(sampleRateHz / updateRateHz);
        mixer->am_TableOffset  = context_size;

        // Allocate a single chip ram block that is big enough to hold all the bits
        size_t chip_size = (mixer->am_PacketSize + mixer->am_PacketSize >> 2);

        BYTE* alloc = AllocCacheAligned(chip_size << 1, MEMF_CHIP); // MEMF_CHIP

        if (!alloc) {
            Aud_FreeMixer(mixer);
            return NULL;
        }

        mixer->am_LeftPacketSamplePtr = alloc;
        mixer->am_LeftPacketVolumePtr = (UWORD*)(alloc + mixer->am_PacketSize);

        mixer->am_RightPacketSamplePtr = alloc + chip_size;
        mixer->am_RightPacketVolumePtr = (UWORD*)(alloc + chip_size + mixer->am_PacketSize);

        Aud_SetMixerVolume(mixer, 8192);
    }
    return mixer;
}

void Aud_FreeMixer(REG(a0, Aud_Mixer* mixer))
{
    if (mixer && mixer->am_LeftPacketSamplePtr) {
        FreeCacheAligned(mixer->am_LeftPacketSamplePtr);
    }
    FreeCacheAligned(mixer);
}

void Aud_SetMixerVolume(
    REG(a0, Aud_Mixer* mixer),
    REG(d0, UWORD volume)
)
{
    /**
     * Generate AUD_8_TO_16_LEVELS-1 tables of 256 words each, intended to be indexed by the (unsigned) sample
     * position to obtain the desired 16-bit value.
     */
    WORD* table_ptr  = (WORD*)((UBYTE*)mixer + mixer->am_TableOffset);
    WORD  table_step = (WORD)(volume / AUD_8_TO_16_LEVELS);
    WORD  table_max  = table_step;
    for (int t = 0; t < (AUD_8_TO_16_LEVELS - 1); ++t) {
        WORD  level_step    = table_max / 128;
        WORD  level         = level_step;

        table_ptr[0] = 0;
        table_ptr[(unsigned)((-128) & 0xFF)] = -table_max;

        for (int i = 1; i < 128; ++i) {
            table_ptr[i]  = level;
            table_ptr[(unsigned)((-i) & 0xFF)] = -level;
            level += level_step;
        }
        table_ptr += 256;
        table_max += table_step;
    }
}

extern void Aud_DumpMixer(
    REG(a0, Aud_Mixer* mixer)
)
{
    printf(
        "Aud_Mixer allocated at %p\n"
        "\tMix Rate      %hu Hz\n"
        "\tUpdate Rate   %hu Hz\n"
        "\tPacket Length %hu samples [%hu lines]\n"
        "\tLeft Sample Packet at  %p\n"
        "\tLeft Volume Packet at  %p\n"
        "\tRight Sample Packet at %p\n"
        "\tRight Volume Packet at %p\n"
        "\tVolume Tables at %p\n"
        "\tAbsMaxL %hu\n"
        "\tAbsMaxR %hu\n"
        "\tShiftL %hu\n"
        "\tShiftR %hu\n"
        "",
        mixer,
        mixer->am_SampleRateHz,
        mixer->am_UpdateRateHz,
        mixer->am_PacketSize,
        mixer->am_PacketSize / CACHE_LINE_SIZE,
        mixer->am_LeftPacketSamplePtr,
        mixer->am_LeftPacketVolumePtr,
        mixer->am_RightPacketSamplePtr,
        mixer->am_RightPacketVolumePtr,
        ((UBYTE*)mixer) + mixer->am_TableOffset,
        mixer->am_AbsMaxL,
        mixer->am_AbsMaxR,
        mixer->am_ShiftL,
        mixer->am_ShiftR

    );

    for (int channel = 0; channel < AUD_NUM_CHANNELS; ++channel) {
        printf(
            "\tChannel %2d: "
            "SamplePtr: %10p [Remaining: %4hu LVol:%2hu RVol:%2hu]\n"
            "",
            channel,
            mixer->am_ChannelState[channel].ac_SamplePtr,
            mixer->am_ChannelState[channel].ac_SamplesLeft,
            (UWORD)mixer->am_ChannelState[channel].ac_LeftVolume,
            (UWORD)mixer->am_ChannelState[channel].ac_RightVolume
        );
    }

    printf("Sample Fetch Buffer [");
    for (int i = 0; i < CACHE_LINE_SIZE; ++i) {
        printf("%+3d, ", (int)mixer->am_FetchBuffer[i]);
    }
    puts("]");

    printf("Left Mix Buffer  [");
    for (int i = 0; i < CACHE_LINE_SIZE; ++i) {
        printf("%+5d, ", (int)mixer->am_AccumL[i]);
    }
    puts("]");

    printf("Right Mix Buffer [");
    for (int i = 0; i < CACHE_LINE_SIZE; ++i) {
        printf("%+5d, ", (int)mixer->am_AccumR[i]);
    }
    puts("]");

}


#include "mixer.h"
#include <stdio.h>
#include <proto/exec.h>


#define CACHE_ALIGN_MASK (CACHE_LINE_SIZE - 1)

/**
 * These are the normalisation factors to use where fast multiplication is available, as 6.8 fixed point. The index
 * of the table is the AUDxVOL-1 of value volume level we are normalising for.
 *
 * Each value is equivalent to (int)(256.0 * 64.0/(index + 1)).
 *
 * As a worked example, suppose a sample packet contains the largest absolute value 1535. We convert the sample to an
 * index using >> 9 which gives us 2. This is 1 less than the AUDxVOL value we will be replaying the normalised 8-bit
 * sample data at.
 *
 * Equally, the value of corresponding index in this table is the value we will multiply all of the samples by in this
 * packet as a 16x16 => 32. Thus, the largest value we will have is 1535 * 5461 = 8382635.
 *
 * The normalised 8 bit value we need is obtained by performing >> 16 on this product, which is just a swap operation
 * in assembler. So, 8382635/65536 = 127.9, which gives 127.
 *
 * Where index + 1 is a perfect power of 2, we instead have a right shift value that will scale a 16-bit value directly
 * to the target range. For example, for a normalisation target of 64 (full volume), right shifting a 16-bit sample
 * by 8 immediately gives you the corresponding 8-bit output.
 *
 * We can easily tell if our index represents a perfect power of 2 by doing (index + 1) & index. If the result is zero
 * then the value at that index needs to be used as a shift, otherwise a multiply.
 */
WORD Aud_NormFactors_vw[64] __attribute__ ((aligned (CACHE_LINE_SIZE)));

WORD Aud_NormFactors_vw[64] = {
       2,    3, 5461,    4,
    3276, 2730, 2340,    5,
    1820, 1638, 1489, 1365,
    1260, 1170, 1092,    6,
     963,  910,  862,  819,
     780,  744,  712,  682,
     655,  630,  606,  585,
     564,  546,  528,    7,
     496,  481,  468,  455,
     442,  431,  420,  409,
     399,  390,  381,  372,
     364,  356,  348,  341,
     334,  327,  321,  315,
     309,  303,  297,  292,
     287,  282,  277,  273,
     268,  264,  260,    8
};

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
        "\tAbsMaxL %hu [Norm Index %hu]\n"
        "\tAbsMaxR %hu [Norm Index %hu]\n"
        "\tNorm Table at %p\n"
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
        mixer->am_IndexL,
        mixer->am_AbsMaxR,
        mixer->am_IndexR,
        Aud_NormFactors_vw
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


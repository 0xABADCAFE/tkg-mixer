
        include "macros.i"

; Size of a cache line in bytes
CACHE_LINE_SIZE     EQU 16

; Number volume steps
AUD_8_TO_16_LEVELS  EQU 16

; Number of channels
AUD_NUM_CHANNELS    EQU 16

    STRUCTURE Aud_ChanelState,0
        APTR  ac_SamplePtr_l ; 4 Current address of the data
        WORD  ac_SamplesLeft_w  ; 2 Remaining number of bytes
        UBYTE ac_LeftVol_b   ; 1 Left volume (0-15)
        UBYTE ac_RightVol_b  ; 1 Right volume (0-15)
        STRUCT_SIZE Aud_ChanelState

    STRUCTURE Aud_Mixer,0

        STRUCT_ARRAY am_ChannelState,Aud_ChanelState,AUD_NUM_CHANNELS ; 8*16

        ; Inline, cache aligned buffers

        ; Contains the next cache line worth of incoming 8-bit sample data
        BYTE_ARRAY am_FetchBuffer_vb,CACHE_LINE_SIZE

        ; Contains the current cache line pair of the accumulated 16-bit sample data
        ; for the left channel
        WORD_ARRAY am_AccumL_vw,CACHE_LINE_SIZE

        ; Contains the current cache line pair of the accumulated 16-bit sample data
        ; for the right channel
        WORD_ARRAY am_AccumR_vw,CACHE_LINE_SIZE

        ; Pointers to the eventual destination buffers in CHIP ram
        APTR am_LPacketSamplePtr_l ; contains normalised 8-bit sample data for the left channel
        APTR am_LPacketVolumePtr_l ; contains 6-bit volume modulation data for the left channel
        APTR am_RPacketSamplePtr_l ; contains normalised 8-bit sample data for the right channel
        APTR am_RPacketVolumePtr_l ; contains 6-bit volume modulation data for the right channel

        UWORD  am_AbsMaxL_w;
        UWORD  am_AbsMaxR_w;
        UWORD  am_IndexL_w;
        UWORD  am_IndexR_w;

        APTR   am_ChipBufferPtr_l ; contains base address of the CHIP ram buffers
        UWORD  am_SampleRateHz_w;
        UWORD  am_UpdateRateHz_w;

        UWORD  am_PacketSize_w;
        UWORD  am_TableOffset_w;

        STRUCT_SIZE Aud_Mixer

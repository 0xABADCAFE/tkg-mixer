
        mc68040

        include "macros.i"

; Size of a cache line in bytes
CACHE_LINE_SIZE      EQU 16

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

        ULONG  am_LinesProcessed_l;
        UWORD  am_AbsMaxL_w;
        UWORD  am_AbsMaxR_w;

        UWORD  am_SampleRateHz_w;
        UWORD  am_UpdateRateHz_w;

        UWORD  am_PacketSize_w;
        UWORD  am_TableOffset_w;

        STRUCT_SIZE Aud_Mixer

        section .text,code
        align 4

        xdef _asm_sizeof_mixer;
        xdef _Aud_MixLine

;
;
;
; a0 points at mixer

_Aud_MixLine::
Aud_MixLine:
        movem.l d2/d3/d4/d5/a2/a3/a4,-(sp)

.clear_accum_buffers:
        move.w  #CACHE_LINE_SIZE-1,d2
        lea     am_AccumL_vw(a0),a1

.clear_loop:
        clr.l   (a1)+
        dbra    d2,.clear_loop

        ; Fixed number of channels to mix in d2
        moveq   #AUD_NUM_CHANNELS-1,d2

        ; Get channelstate array into a1
        lea     am_ChannelState(a0),a1

.next_channel:
        ; Get the channel sample data pointer in a2, skip if null
        move.l  ac_SamplePtr_l(a1),a2
        beq.s   .done_channel

        ; Check there if data left to process. This really should never happen
        tst.w   ac_SamplesLeft_w(a1)
        beq.s   .done_channel

        ; Get the left/right volume pair, each of which should be 0-15, with 0 being a silence skip
        move.w   ac_LeftVol_b(a1),d5

        ; Enforce the range 0-15 for each channel
        and.w    #$0F0F,d5

        ; If both are zero, just update the channel state and move along
        beq.s   .update_channel

.not_silent:
        ; swap the bytes in d5 to get the left voume in the lower byte first. Endian fail, lol.
        rol.w   #8,d5

        ; grab the next 16 samples
        lea     am_FetchBuffer_vb(a0),a3

        ; The theory goes, we won't be crapflooding the datacache with the sample data this way...
        move16  (a2)+,(a3)+

        ; Two step loop. The first iteration handles the left channel, the second iteration handles the right
        move.w  #1,d3
        lea     am_AccumL_vw(a0),a4 ; note that the right accumulator immediately follows
        clr.l   d0

.mix_samples:
        move.b  d5,d0   ; d0 = 0-15, 0 silence, 1-14 are volume table selectors
        beq.s   .update_channel

        sub.w   #1,d0   ; d0 = 0-14, now we need to multiply by 512 to get the table start
        lsl.w   #8,d0   ;
        add.w   d0,d0   ; d0 = table position = vol * 256 * sizeof(WORD)

        ; Add the structure offset and put the effective address into a2
        add.w   am_TableOffset_w(a0),d0
        lea     (a0,d0.w),a2

        ; Point a3 at the cache line of samples we loaded
        lea     am_FetchBuffer_vb(a0),a3

        move.w  #CACHE_LINE_SIZE-1,d1    ; num samples in d1

        ; Index the table by sample value (as unsigned word)
        clr.w   d0

.next_sample:
        move.b  (a3)+,d0         ; next 8-bit sample.
        move.w  (a2,d0.w*2),d4   ; look up the volume adjusted word
        add.w   d4,(a4)+         ; accumulate onto the target buffer
        dbra    d1,.next_sample

        ; Now do the second step for the opposite side...
        lsr.w   #8,d5
        dbra    d3,.mix_samples

.update_channel:
        sub.w   #CACHE_LINE_SIZE,ac_SamplesLeft_w(a1)
        bne.s   .inc_sample_ptr

        ; zero out the remaining channel state
        clr.l   ac_SamplePtr_l(a1)
        clr.w   ac_LeftVol_b(a1)
        bra.s   .done_channel

.inc_sample_ptr:
        add.l   #CACHE_LINE_SIZE,ac_SamplePtr_l(a1)

.done_channel:
        add.w   #Aud_ChanelState_SizeOf_l,a1

        ; WEIRD!!! No idea why, but unless I trigger a write here, nothing happens
        add.l  #1,am_LinesProcessed_l(a0)

        dbra    d2,.next_channel

        ; Now we need to find the maximum sbsolute value of each accumulation buffer
        lea     am_AccumL_vw(a0),a4

        clr.w   d0 ; d0 will contain the next absolute value from the buffer
        clr.l   d2

        ; Same two step trick as before, we process left then right consecutively
        move.w  #1,d3

.next_buffer:
        move.w  #CACHE_LINE_SIZE-1,d1

.next_buffer_value:
        move.w  (a4)+,d0
        bge.s   .not_negative

        neg.w   d0

.not_negative:
        ; d2 is tracking the maximum
        cmp.w   d0,d2
        bge.s   .not_bigger

        move.w  d0,d2

.not_bigger:
        dbra    d1,.next_buffer_value

        swap    d2
        dbra    d3,.next_buffer

        ; write both back at once
        move.l  d2,am_AbsMaxL_w(a0)

.finished:
        movem.l (sp)+,d2/d3/d4/d5/a2/a3/a4
        rts


_asm_sizeof_mixer::
        dc.w Aud_Mixer_SizeOf_l

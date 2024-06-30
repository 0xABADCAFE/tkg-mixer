
        mc68040

        include "mixer_asm.i"

        section .text,code
        align 4

        xdef _asm_sizeof_mixer;
        xdef _Aud_MixPacket_040

        xref _Aud_NormFactors_vw;


; a0 points at mixer

_Aud_MixPacket_040::
Aud_MixPacket_040:
        movem.l d2-d6/a2-a4,-(sp)

        ; Number of lines to mix in d6
        move.w  am_PacketSize_w(a0),d6
        lsr.w   #4,d6
        subq.w  #1,d6


        ; Reset the working pointers
        lea     am_LPacketSamplePtr_l(a0),a1
        lea     am_LPacketSampleBasePtr_l(a0),a2
        move.l  (a2)+,(a1)+
        move.l  (a2)+,(a1)+
        move.l  (a2)+,(a1)+
        move.l  (a2)+,(a1)+

.mix_next_line:
        swap    d6

;
; Initialisation - clear out the accumulation buffers
;
.clear_accum_buffers:
        move.w  #CACHE_LINE_SIZE-1,d2
        lea     am_AccumL_vw(a0),a1

.clear_loop:
        clr.l   (a1)+
        dbra    d2,.clear_loop

;
; Mixing - Iterate the channel state list. For each channel that has an active pointer and samples remaining,
;          transfer a packet to the fetch buffer. For 040 and 060 this is done using move16, so that we arent
;          slowly churning out all the datacache.
;
        ; Fixed number of channels to mix in d2
        moveq   #AUD_NUM_CHANNELS-1,d2

        ; Get channelstate array into a1
        lea     am_ChannelState(a0),a1

.next_channel:
        ; Get the channel sample data pointer in a2, skip if null. We need to move to a data register to set the CC
        move.l  ac_SamplePtr_l(a1),d0
        beq   .done_channel

        move.l  d0,a2

        ; Check there if data left to process. This really should never happen
        tst.w   ac_SamplesLeft_w(a1)
        beq   .done_channel

        ; Get the left/right volume pair, each of which should be 0-15, with 0 being a silence skip
        move.w  ac_LeftVol_b(a1),d5

        ; Enforce the range 0-15 for each channel
        and.w   #$0F0F,d5

        ; If both are zero, just update the channel state and move along
        beq.s   .update_channel

.channel_not_silent:
        ; swap the bytes in d5 to get the left voume in the lower byte first. Endian fail, lol.
        rol.w   #8,d5

        ; grab the next 16 samples
        lea     am_FetchBuffer_vb(a0),a3

        ; The theory goes, we won't be crapflooding the datacache with the sample data this way...
        move16  (a2)+,(a3)+

        ; Two step loop. The first iteration handles the left channel, the second iteration handles the right
        moveq   #1,d3
        lea     am_AccumL_vw(a0),a4 ; note that the right accumulator immediately follows
        clr.l   d0

;
; Accumulation - For each 8-bit sample in the fetch buffer, look up the 16-bit value in the volume table and
;                add to the values in the accumulation buffer


; We are going to use table lookup for our sample frame.
.mix_samples:
        move.b  d5,d0   ; d0 = 0-15, 0 silence, 1-14 are volume table selectors
        beq.s   .mix_next_buffer

        subq.w  #1,d0   ; d0 = 0-14, now we need to multiply by 512 to get the table start
        lsl.w   #8,d0   ;
        add.w   d0,d0   ; d0 = table position = vol * 256 * sizeof(WORD)

        ; Add the structure offset and put the effective address into a2
        add.w   am_TableOffset_w(a0),d0
        lea     (a0,d0.w),a2

        ; Point a3 at the cache line of samples we loaded
        lea     am_FetchBuffer_vb(a0),a3

        moveq   #CACHE_LINE_SIZE-2,d1    ; num samples in d1

        ; Index the table by sample value (as unsigned word)
        clr.w   d0

        ; d0 temp
        ; d1.w sample count
        ; d2.w channel count
        ; d3.w LR pass
        ; d4 temp
        ; d5.w vol pair
        ; d6.w frame count

.mix_first_sample:
        move.b  (a3)+,d0         ; next 8-bit sample.
        move.w  (a2,d0.w*2),d4   ; look up the volume adjusted word
        add.w   d4,(a4)+         ; accumulate onto the target buffer
        move.w  d0,d6            ; d6.w contains last 8-bit sample value

.mix_next_sample:
        neg.b   d0               ; Calculate the next 8-bit delta in d0
        move.b  (a3)+,d6         ; Next 8-bit sample in d6
        add.b   d6,d0            ; 8-bit delta in d0
        add.w   (a2,d0.w*2),d4   ; Add looked up 16-bit delta to last 16-bit sample
        add.w   d4,(a4)+         ; Accumulate
        move.b  d6,d0
        dbra    d1,.mix_next_sample

.mix_next_buffer:
        ; Now do the second step for the opposite side...
        lsr.w   #8,d5
        dbra    d3,.mix_samples

.update_channel:
        sub.w   #CACHE_LINE_SIZE,ac_SamplesLeft_w(a1)
        bne.s   .inc_sample_ptr

        ; Zero out the remaining channel state if we exhausted it
        clr.l   ac_SamplePtr_l(a1)
        clr.w   ac_LeftVol_b(a1)
        bra.s   .done_channel

.inc_sample_ptr:
        add.l   #CACHE_LINE_SIZE,ac_SamplePtr_l(a1)

.done_channel:
        lea     Aud_ChanelState_SizeOf_l(a1),a1

        dbra    d2,.next_channel


; Peak Level Analysis - Find the peak level of the left and right accumulation buffers so that we can normalise
;                       each one and convert to 8-bit data with a corresponding chanenel volume attenuation.
;
        ; Now we need to find the maximum absolute value of each accumulation buffer
        lea     am_AccumL_vw(a0),a4
        lea     am_AbsMaxL_w(a0),a2

        ; Same two-step trick as before, we process left then right consecutively
        moveq  #1,d3

        ; Peak value / 512 gives us our normalisation index
        moveq  #9,d4

.next_buffer:
        clr.w   d0 ; d0 will contain the next absolute value from the buffer
        clr.l   d2
        moveq  #CACHE_LINE_SIZE-1,d1

.next_buffer_value:
        move.w  (a4)+,d0
        bge.s   .not_negative

        neg.w   d0

.not_negative:
        cmp.w   d0,d2
        bgt.s   .not_bigger

        move.w  d0,d2

.not_bigger:
        dbra    d1,.next_buffer_value

        ; peak value (15 bit) - we don't really need to store this but it's just for checking
        move.w  d2,(a2)+

        ; Now determine the normalisation factor. This is just the 15-bit absolute peak >> 9
        ; which gives us our offset into the _Aud_NormFactors_vw table
        lsr.w   d4,d2
        move.w  d2,2(a2)

        dbra    d3,.next_buffer

; Normalisation - For each 16-bit value in the accumulation buffer, scale by the normalisation value and then
;                 convert to 8 bit.

        ; Same two-step trick as before, we process left then right consecutively
        moveq  #1,d3

        lea     am_AccumL_vw(a0),a2
        lea     am_IndexL_w(a0),a3
        lea     am_LPacketSamplePtr_l(a0),a4

.normalize_next:
        ; get the table index into d1. If the index is on less than a power of 2, we will be using a shift method
        moveq   #1,d0
        move.w  (a3),d1                ; Index that we calculated in the analysis step
        lea     _Aud_NormFactors_vw,a1
        move.w  (a1,d1.w*2),d2         ; d2 contains normalisation factor

        move.l  4(a4),a1               ; volume packet pointer in a1
        add.w   d1,d0                  ; i + 1
        move.w  d0,(a1)+               ; write volume value

        move.l  a1,4(a4)               ; updated working volume pointer

        moveq   #(CACHE_LINE_SIZE/4)-1,d4 ; we are converting 4 samples per loop

        move.l (a4),a1                    ; destination ptr in a1

        ; Check for a perfoect power of 2..
        and.w   d1,d0                  ; (i + 1) & i
        beq     .shift_norm_four  ;

.mul_norm_four:
        ; something like this, for 060
        move.w  (a2)+,d0    ; xx:xx:AA:aa
        muls.w  d2,d0       ; 00:AA:xx:xx
        lsr.l   #8,d0       ; 00:00:AA:xx
        move.w  d0,d1       ; xx:xx:AA:xx

        move.w  (a2)+,d0    ; xx:xx:BB:bb
        muls.w  d2,d0       ; xx:BB:xx:xx
        swap    d0          ; xx:xx:xx:BB
        move.b  d0,d1       ; xx:xx:AA:BB
        lsl.l   #8,d1       ; xx:AA:BB:00

        move.w  (a2)+,d0    ; xx:xx:CC:cc
        muls.w  d2,d0       ; xx:CC:xx:xx
        swap    d0          ; xx:xx:xx:CC
        move.b  d0,d1       ; xx:AA:BB:CC
        lsl.l   #8,d1       ; AA:BB:CC:00

        move.w  (a2)+,d0    ; xx:xx:DD:dd
        muls.w  d2,d0       ; xx:DD:xx:xx
        swap    d0          ; xx:xx:xx:DD
        move.b  d0,d1       ; AA:BB:CC:DD

        move.l  d1,(a1)+    ; long slow chip write here

        dbra    d4,.mul_norm_four

        move.l  a1,(a4)     ; update working destination pointer

        bra.s   .done_channel_normalise

.shift_norm_four:

        ; process samples in pairs

        move.l  (a2)+,d0 ; AA:aa:BB:bb
        lsr.l   d2,d0    ; 00:AA:xx:BB
        move.l  (a2)+,d1 ; CC:cc:DD:dd
        lsl.w   #8,d0    ; 00:AA:BB:00
        lsr.l   d2,d1    ; xx:CC:xx:DD
        lsl.l   #8,d0    ; AA:BB:00:00
        lsl.w   #8,d1    ; xx:CC:DD:00
        lsr.l   #8,d1    ; 00:xx:CC:DD
        move.w  d1,d0    ; AA:BB:CC:DD
        move.l  d0,(a1)+ ; long slow chip write

        dbra    d4,.shift_norm_four

        move.l  a1,(a4)     ; update working destination pointer

.done_channel_normalise:
        lea     2(a3),a3              ; next index
        lea     8(a4),a4              ; next buffer pair

        dbra    d3,.normalize_next

        swap    d6

        dbra    d6,.mix_next_line

.finished:
        movem.l (sp)+,d2-d6/a2-a4
        rts


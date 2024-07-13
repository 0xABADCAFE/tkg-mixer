

;//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
;//
;//  Linear Lookup (68040) - Each 8-bit sample is looked up directly in the volume table.
;//
;//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

_Aud_MixPacket_040Null::
Aud_MixPacket_040Null:
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

; No mixing ///////////////////////////////////////////////////////////////////////////////

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

        ; No normalisation.

        ; chip writes - just push silence

        clr.l   d0
        lea     am_LPacketSamplePtr_l(a0),a4
        move.l  4(a4),a1               ; volume packet pointer in a1
        move.w  d0,(a1)+               ; write volume value
        move.l  a1,4(a4)               ; save pointer

        move.l  (a4),a1                ; sample packet pointer in a1
        move.l  d0,(a1)+
        move.l  d0,(a1)+
        move.l  d0,(a1)+
        move.l  d0,(a1)+
        move.l  a1,(a4)                ; save pointer


        lea     am_RPacketSamplePtr_l(a0),a4
        move.l  4(a4),a1               ; volume packet pointer in a1
        move.w  d0,(a1)+               ; write volume value
        move.l  a1,4(a4)               ; save pointer

        move.l  (a4),a1                ; sample packet pointer in a1
        move.l  d0,(a1)+
        move.l  d0,(a1)+
        move.l  d0,(a1)+
        move.l  d0,(a1)+
        move.l  a1,(a4)                ; save pointer

        dbra    d6,.mix_next_line

.finished:
        movem.l (sp)+,d2-d6/a2-a4
        rts


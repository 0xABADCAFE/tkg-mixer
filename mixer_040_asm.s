
        mc68040

        include "mixer_asm.i"

        section .text,code
        align 4

        xdef _asm_sizeof_mixer;
        xdef _Aud_MixPacket_040Delta
        xdef _Aud_MixPacket_040Linear
        xdef _Aud_MixPacket_040Shifted

        xref _Aud_NormFactors_vw;

        include "68040/null.s"
        include "68040/linear.s"
        include "68040/delta.s"
        include "68040/predelta.s"
        include "68040/shifted.s"

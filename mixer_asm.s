
        mc68040

        include "mixer_asm.i"


        section .text,code
        align 4

        xdef _asm_sizeof_mixer;

        xref _Aud_NormFactors_vw;


_asm_sizeof_mixer::
        dc.w Aud_Mixer_SizeOf_l

# tkg-mixer
Greenfield Sound Mixer for [AB3D2-TKG](https://github.com/mheyer32/alienbreed3d2)

## About
The purpose of this repository is to experiment design and implement a new sound engine for the game. The original features the same 4 or 8 channel playback mode of the original AB3D game, with the following limitations:
- Fixed 8kHz playback rate. 
- 7-bit samples:
    - This allows the 8-channel mode to mix two samples into one channel without the risk of overflow but sacrifices fidelity.
- Single channel mod plabyack.
    - In practise, this reserves a hardware channel and thus restricts the number of available to either 3 or 8. 

The aim is to replace the sound engine with something more sophisticated:
- Customisable rate
    - Mixing/Playback rate
    - Update rate
- 16 fully 8-bit input channels:
    - Independent left and right volume per channel.
    - Scaled and mixed to 16-bit
- Gain Adjusted Paula Playback based on [Paula HDR](https://github.com/0xABADCAFE/paula-hdr)
    - 16-bit mixed data normalised to 8-bit sample + volume data
    - Hardware 8-bit channel volume register is modulated in hardware by a second channel that consumes the volume data.
 - Streamed music
    - The planned playback method requires all 4 hardware channels, meaning that the module plabyack is no longer possible.
    - An alternative would be to include a music stream into the mixer. This does not have to be as an existing channel but could be specialised mechanism.

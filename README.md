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
    - Playback data to be composed of frames of 16 8-bit samples to be played at a given hardware volume. 
 - Streamed music
    - The planned playback method requires all 4 hardware channels, meaning that the module plabyack is no longer possible.
    - An alternative would be to include a music stream into the mixer. This does not have to be as an existing channel but could be specialised mechanism.

## Considerations
The game already has quite high system requirements. Consequently, the aim is to design with 68040/68060/Emulation in mind. This section is a bit of a brain dump.

### CPU Considerations
| Target | Feature | Implications |
| - | - | - |
| 68040 | 4KiB Datacache | Fast working set | 
| 68040 | move16 | Non cache-polluting transfers |
| 68060 | 8KiB Datacache | Fast working set |
| 68060 | move16 | Non cache-polluting transfers |
| 68060 | Fast multiply | Arithemtic can take less time than lookup tables |
| Emu68 | Ludicrous Speed | Anything goes, but identifies as a 68040 |

Considering the above, an ideal implementation should:
- Use move16 to copy source data for mixing to a cached location in the working set.
    - This prevents filling of the data cache with data that won't be immediately reused.
    - There are some caveats around move16 that mean we should have the option of not using it.
-  Prefer multiplication over lookup on 68060 for tasks such as applying volume, normalisation etc.
-  Consider the cache when implementing lookup tables.
    - Scattered access will result in cache misses and line transfers.
 
### Sound Hardware Considerations
Paula provides 4 channels of 8-bit audio with looping DMA playback from buffers in Chip RAM and provides an interrupt when about to loop. The other key feature it provides is the ability to use channels as a modulation source for volume and/or period of another channel. The mixer will produce 8-bit audion for plabyack and simultaneously 6-bit volume data in a 16:1 ratio - i.e. 1 volume update per 16 sample points.

In order for the sound to be heard, the mixer has to write the results of mixing into Chip RAM buffers. The Chip RAM bus is 14MHz, 32-bits wide and contended. Writes are therefore extremely slow. The CPU an continue executing instructions while those writes are pending. This means that some of the computational costs can be hidden.

### Cache Considerations
Mixing from multiple input buffers into an output buffer is fundamentally a stream processing task. Naive reading from the source will benefit from cache line transfers giving some readahad but the caches will soon be filled and other data evicted. Both the 68040 and 68060 provide a move16 instrution that can transfer a cache line worth of data from one location to another without allocating any new cache entries. Better use of the cache can be achieved by:
- Having a small, repeatedly used buffer for sample data.
- Transferring data from the source samples to the buffer using move16

Data should be organised in a manner that facilitates cache line transfers:
- Aligned to cache line size (16 bytes)
- Processed in (multiples of) cache line size.

For 68060, fast multiplication reduces the dependency on lookup tables and most operations can be based on it. However, for 68040, multiplication is too expensive to use as liberally. Precomputed lookup tables will be necessary, e.g. for a given channel volume, a table that maps an input 8-bit sample value to an output 16-bit sample. Such a table will comprise of 256 words (128 is possible if sign is handled separately) and there would need to be N-1 of them for N volume levels (sinc volume 0 is a special case).

Assuming a simple 256-entry lookup per volume level the volume table occupies 512 bytes, i.e. 32 cache lines. In the worst possible case, each of the 16 input samples looks up a value in a different cache line, resulting in 16 cache line loads. However, most real audio data tends to be more predictable than this. A simulation using 512KB of 8-bit audio shows that the vast majority of accesses happen within the first and last four cache line's worth of the table.

A way to improve this significantly is to delta encode the 8-bit sample data, which causes the accesses to map to just the first and last line in the table. This mechanism still works because the effect of volume scaling the absolute sample value or the delta between any pair, is the same.

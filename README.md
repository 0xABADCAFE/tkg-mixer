# tkg-mixer
Greenfield Sound Mixer for [AB3D2-TKG](https://github.com/mheyer32/alienbreed3d2)

## About
The purpose of this repository is to experiment design and implement a new sound engine for the game. The original features the same 4 or 8 channel playback mode of the original AB3D game, with the following limitations:
- Fixed 8kHz playback rate.
- 7-bit samples:
    - This allows the 8-channel mode to mix two samples into one channel without the risk of overflow but sacrifices fidelity.
- Single channel mod playback.
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
 - Streamed music:
    - The planned playback method requires all 4 hardware channels, meaning that the module playback is no longer possible.
    - An alternative would be to include a music stream into the mixer. This does not have to be as an existing channel but could be specialised mechanism.

## Design

The overall design is illustrated below:

![Line Hit Rate Simulations](./design/design.png)

Overview:

- 8-bit sample data are stored in Fast RAM.
- A set of Channel state structures (one per channel) each maintain:
    - The current pointer to the sample data, or null if no sample is playing
    - The remaining number of samples
    - The left and right volume of the channel in the the stereo field.

- Sound is fetched from the Sample Data into an internal buffer for mixing:
    - This transfer is intended to make use of cache line moves in the 040/060 to avoid polluting the data cache.
    - Depending on the CPU, lookup tables or direct multiplcation is used to scale the 8-bit sample data into a 16-bit intermediate.
    - The 16-bit intermediate data are accumulated into a Mixing Buffer.

- Dynamics analysis is performed on the Mixing Buffer. This determines two values:
    - A scale factor for the 16-bit mixed data that permits conversion to 8-bit
    - The ideal hardware channel volume for Paula to replay the 8-bit data at.
    - The resulting sample and volume data are transferred to DMA accessible buffers in Chip RAM.

- Paula is configured to use 2 hardware channels:
    - A carrier channel plays the 8-bit sample data.
    - A second channel is configured to modulate the volume of the carrier and plays the volume data.
    - All 4 channels are be used to permit full stereo output.

- The overall result is that 8-bit samples are mixed without loss of precision into the mixing buffer. The resulting 16-bit stream is then converted into a companded 8-bit format for direct hardware replay by Paula.
- Important points to note regarding hardware volume mdulation:
   - Does not affect the 8-bit DAC precision.
   - Is highly linear, unlike the actual 8-bit DAC itself which has some nonlinearity.
   - Paula accesses data a word at a time:
       - For sample data, this corresponds to a pair
       - When used as a volume modulator, the word comprises a single volume value.
       - This means that when the carrier and modulator are operating at the same frequency, the volume can only be modulated at half the sample rate.
    - The intention is that successive packets of 16 samples will be played at an ideal volume determined for the entire packet.

## Considerations
The game already has quite high system requirements. Consequently, the aim is to design with 68040/68060/Emulation in mind. This section is a bit of a brain dump.

### Concepts
- **Frame**: A set of 16 sample values that will be processed together, including fetching and normalisation to a given volume, for playback.
- **Packet**: A number of _frames_ (min 1) that will be processed in a single mixing update operation.
- **Line**: Any set of data that is aligned to and accessible as a cache line.

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
Paula provides 4 channels of 8-bit audio with looping DMA playback from buffers in Chip RAM and provides an interrupt when about to loop. The other key feature it provides is the ability to use channels as a modulation source for volume and/or period of another channel. The mixer will produce 8-bit audio for playback and simultaneously 6-bit volume data in a 16:1 ratio - i.e. 1 volume update per 16 sample points.

In order for the sound to be heard, the mixer has to write the results of mixing into Chip RAM buffers. The Chip RAM bus is 14MHz, 32-bits wide and contended. Writes are therefore extremely slow. The CPU an continue executing instructions while those writes are pending. This means that some of the computational costs can be hidden.

### Cache Considerations
Mixing from multiple input buffers into an output buffer is fundamentally a stream processing task. Naive reading from the source will benefit from cache line transfers giving some readahad but the caches will soon be filled and other data evicted. Both the 68040 and 68060 provide a move16 instrution that can transfer a cache line worth of data from one location to another without allocating any new cache entries. Better use of the cache can be achieved by:
- Having a small, repeatedly used buffer for sample data.
- Transferring data from the source samples to the buffer using move16

Data should be organised in a manner that facilitates cache line transfers:
- Aligned to cache line size (16 bytes)
- Processed in (multiples of) cache line size.

### 68060
For 68060, fast multiplication means that tasks such as converting an 8-bit input sample at a given volume into a 16-bit intermediate for mixing, can be done in entirely the ALU. There is no need for any precoputed tables so the role of the datacache in these operations is not particularly important.

### 68040
For the 68040, multiplication is far more expensive. The use of lookup tables to convert an 8-bit input sample at a given volume into a 16-bit intermediate is unavoidable and therefore the role of cache is much more important.

The most naive implementation would require a 256-entry lookup table of the 16-bit intermediate, per volume level. We have 32 lines that can be hit for a given 8-bit input. In the worst case, each input value in a frame maps to a different line location, resulting in 16 cache line reads during conversion of that frame. However, real audio data tends to be more predictable than this.

A simulation was performed with approximately 512KiB of 8-bit audio in order to assess the hit rate on each line. As expected, the first and last lines of the table had the highest hit rates (the entries beyond index 128 correspond to negative values, approaching -1 for index 255):

 | Line |  Access | Hit % |
 | - | - | - |
 |    0 |   76389 | 14.61 |
 |    1 |   70581 | 13.50 |
 |    2 |   43585 |  8.34 |
 |    3 |   25762 |  4.93 |
 |    4 |   16787 |  3.21 |
 |    5 |   10519 |  2.01 |
 |    6 |    6788 |  1.30 |
 | ... | ... | < 1 % |
 |   24 |    6228 |  1.19 |
 |   25 |    8125 |  1.55 |
 |   26 |   11435 |  2.19 |
 |   27 |   17149 |  3.28 |
 |   28 |   28256 |  5.40 |
 |   29 |   43274 |  8.28 |
 |   30 |   61325 | 11.73 |
 |   31 |   71899 | 13.75 |

This access pattern is still not ideal, given that there will be up to 16 frames to mix and each frame having independent left and right volumes. We only have 4KiB of data cache. To address this, one obvious solution is to first convert the 8-bit sample into a difference from the previous one. This delta value can then be looked up instead and the resulting 16-bit value considered as a delta to be added to a running 16-bit value for the frame. Doing this results in significantly improved hit rates on the first lines:

 | Line |  Access | Hit % |
 | - | - | - |
 |    0 |  249939 | 47.81 |
 |    1 |   34017 |  6.51 |
 |    2 |   11089 |  2.12 |
 | ... | ... | < 1 % |
 |   29 |   10186 |  1.95 |
 |   30 |   28590 |  5.47 |
 |   31 |  176334 | 33.73 |

Implementing this delta mechanism across frames presents a number of complexities that it would be nice to avoid. Conversely, a variant in which the first lookup is linear and the subsequent 15 are delta was also simulated:

 | Line |  Access | Hit % |
 | - | - | - |
 |    0 |  238106 | 45.55 |
 |    1 |   36702 |  7.02 |
 |    2 |   12922 |  2.47 |
 |    3 |    5635 |  1.08 |
 | ... | ... | < 1 % |
 |   28 |    5570 |  1.07 |
 |   29 |   12210 |  2.34 |
 |   30 |   30481 |  5.83 |
 |   31 |  170615 | 32.64 |

This is still significantly better than the linear case. For completeness, a plot of hit rates for each method on all 32 lines is shown below. Note the Y axis is log scaled in order to better visualise the differences.
![Line Hit Rate Simulations](./design/LUT_CacheLog.png)

The performance of the cache could be improved by storing only the positive values in these tables, halving the storage required. However, this needs to be weighed against the cost of dealing with the sign handling.

## Real Hardware Tests

Testing of the mixing and normalisation logic was performed on both 68060 and 68040 hardware. The following tests were performed using an 8-bit sound file bytes mixed between 1-16 channels. Source channels were spread between left and right and were offset slightly to ensure that the same immediate data for each active channel was not repeatedly refetched. In any case, since source data are fetched using move16, cache should not have been a factor.

- Sound Length: 60460 bytes
- Mixing rate: 16000 Hz
- Update rate: 50Hz
- Packet Size: 320 samples
- Total Mixed: 189
- Measurement: EClock, 709379 Hz

The following test cases were executed:

- Null: The data are fetched and output written but no mixing logic is executed. This gives the baseline IO contribution to the total time.
- Mul: 8-bit samples are multiplied by the L/R channel volumes to obtain the 16-bit intermediate for mixing.
- Shift: 8-bit samples are shifted by the neareast power of 2 approximant of the L/R channel volume to obtain the 16-bit intermediate for mixing.
- LUT: 8-bit samples are converted to 16-bit intermediates using a LUT for the L/R volume levels.
- Delta LUT: 8-bit samples are converted on-the-fly to delta value and looked up using a LUT for the L/R volume levels (tighter cache hit) and then integrated to produce the 16-bit intermediate for mixing.
- Delta LUT PreEnc: 8-bit samples are first pre-encoded into frames of 1 linear sample followed by 15 delta values that are looked up using the L/R volume levels (tighter cache hit) and then integrated to produce the 16-bit intermediate for mixing.

In all cases, normalisation uses multiplication or shift, per 16-sample frame, depending on whether or not the normalisation factor lookup is an exact power of 2 or not.

The expectation was that for the 68060, the Mul test case should perform best, whereas for 68040, the multiplicaton should be more costly than the other options.

### 68060 / 50MHz Results

The raw test results for the 68060 @ 50MHz were as follows. All timing values are in EClock counts.

| **Channels** | **Null** | **Mul** | **Shift** | **LUT** | **Delta LUT** | **Delta LUT PreEnc** |
| ----- | ----- | ----- | ----- | ----- | ----- | ----- |
| **1** | 28091 | 47943 | 48941 | 50908 | 51653 | 50401 |
| **2** | 32478 | 64917 | 68683 | 73036 | 75233 | 72371 |
| **3** | 34929 | 79624 | 82186 | 91123 | 94751 | 90496 |
| **4** | 37561 | 94172 | 96897 | 109890 | 114344 | 109184 |
| **5** | 39778 | 108015 | 111798 | 127994 | 134937 | 127111 |
| **6** | 42932 | 122202 | 126199 | 145942 | 155077 | 146079 |
| **7** | 45105 | 136846 | 141283 | 165852 | 175402 | 164857 |
| **8** | 48237 | 151261 | 155747 | 186309 | 197675 | 184502 |
| **9** | 50233 | 165008 | 170288 | 202551 | 216831 | 203924 |
| **10** | 52975 | 179984 | 185352 | 222043 | 236820 | 221640 |
| **11** | 55874 | 193753 | 199467 | 239704 | 255212 | 239556 |
| **12** | 58548 | 208361 | 214552 | 258584 | 275194 | 256937 |
| **13** | 60968 | 222461 | 229121 | 275865 | 294581 | 277273 |
| **14** | 64066 | 236959 | 243713 | 294273 | 315643 | 294712 |
| **15** | 65825 | 250189 | 257308 | 312876 | 332874 | 314401 |
| **16** | 67801 | 260117 | 267358 | 323808 | 346663 | 325140 |

![68060 50MHz](./doc_images/68060_50_results.png)

### 68040 / 40MHz Results

The raw test results for the 68060 @ 50MHz were as follows. All timing values are in EClock counts.

| **Channels** | **Null** | **Mul** | **Shift** | **LUT** | **Delta LUT** | **Delta LUT PreEnc** |
| ----- | ----- | ----- | ----- | ----- | ----- | ----- |
| **1** | 41524 | 112562 | 100412 | 102473 | 103494 | 101889 |
| **2** | 45230 | 172102 | 142452 | 143769 | 146675 | 142785 |
| **3** | 48619 | 227313 | 181257 | 179946 | 184470 | 178062 |
| **4** | 52148 | 281511 | 220722 | 215978 | 221333 | 214533 |
| **5** | 56098 | 336082 | 260411 | 262979 | 265491 | 253530 |
| **6** | 59533 | 399388 | 299921 | 302454 | 311530 | 295583 |
| **7** | 63265 | 446608 | 340293 | 344982 | 358642 | 335529 |
| **8** | 67060 | 499864 | 378202 | 387834 | 393745 | 373664 |
| **9** | 70024 | 554432 | 417917 | 424685 | 431983 | 411765 |
| **10** | 74019 | 608261 | 456530 | 461663 | 469907 | 446064 |
| **11** | 77708 | 661827 | 495774 | 500451 | 509093 | 479114 |
| **12** | 82480 | 722105 | 542505 | 535603 | 544752 | 514391 |
| **13** | 84609 | 771687 | 574277 | 580778 | 586409 | 553134 |
| **14** | 88622 | 825086 | 613807 | 615716 | 633521 | 592174 |
| **15** | 92185 | 878710 | 654961 | 655865 | 666312 | 631658 |
| **16** | 95312 | 910019 | 675072 | 680289 | 688416 | 647546 |

![68040 40MHz](./doc_images/68040_40_results.png)

The difference between the LUT and Delta LUT Pre Encoded shows the impact of the improved cache hit rate but the result is somewhat marginal in practise. Since the pre-encoding would happen at load time and the end result is consistently better than the multiplication path, there is no real reason not to use it.

## Analysis

At all channel counts, the multiplication based mixing path is fastest for the 68060. This matches the expectation based on the 2-3 cycle time for the operation on this CPU. The 68060 at 50MHz can mix 16 channels in less time than the 68040 at 40MHz can mix 4 using the same code.

For the 68040, there is no meaningful differnce between the Shift and LUT based mixing approaches, with the Delta LUT PreEnc having marginally better performance beyond 3 channels. This means that there is no point in sacrificing the mixing quality using the Shift approach since the peformance is not significantly better than any of the LUT approaches that would produce a result equivalent to the multiplciation path.

Dividing all values by the total packet count of 189 and converting EClocks to actual elapsed time, we can get the total time to mix and convert 20ms of audio corresponding to a single update.

### 68060 Time Per Packet (20ms), in ms

| **Channels** | **Null** | **Mul** | **Shift** | **LUT** | **Delta LUT** | **Delta LUT PreEnc** |
| ----- | ----- | ----- | ----- | ----- | ----- | ----- |
| **1** | 0.21 | 0.36 | 0.37 | 0.38 | 0.39 | 0.38 |
| **2** | 0.24 | 0.48 | 0.51 | 0.54 | 0.56 | 0.54 |
| **3** | 0.26 | 0.59 | 0.61 | 0.68 | 0.71 | 0.67 |
| **4** | 0.28 | 0.70 | 0.72 | 0.82 | 0.85 | 0.81 |
| **5** | 0.30 | 0.81 | 0.83 | 0.95 | 1.01 | 0.95 |
| **6** | 0.32 | 0.91 | 0.94 | 1.09 | 1.16 | 1.09 |
| **7** | 0.34 | 1.02 | 1.05 | 1.24 | 1.31 | 1.23 |
| **8** | 0.36 | 1.13 | 1.16 | 1.39 | 1.47 | 1.38 |
| **9** | 0.37 | 1.23 | 1.27 | 1.51 | 1.62 | 1.52 |
| **10** | 0.40 | 1.34 | 1.38 | 1.66 | 1.77 | 1.65 |
| **11** | 0.42 | 1.45 | 1.49 | 1.79 | 1.90 | 1.79 |
| **12** | 0.44 | 1.55 | 1.60 | 1.93 | 2.05 | 1.92 |
| **13** | 0.45 | 1.66 | 1.71 | 2.06 | 2.20 | 2.07 |
| **14** | 0.48 | 1.77 | 1.82 | 2.19 | 2.35 | 2.20 |
| **15** | 0.49 | 1.87 | 1.92 | 2.33 | 2.48 | 2.35 |
| **16** | 0.51 | 1.94 | 1.99 | 2.42 | 2.59 | 2.43 |

### 68060 Time Per Packet (20ms), in ms

| **Channels** | **Null** | **Mul** | **Shift** | **LUT** | **Delta LUT** | **Delta LUT PreEnc** |
| ----- | ----- | ----- | ----- | ----- | ----- | ----- |
| **1** | 0.31 | 0.84 | 0.75 | 0.76 | 0.77 | 0.76 |
| **2** | 0.34 | 1.28 | 1.06 | 1.07 | 1.09 | 1.06 |
| **3** | 0.36 | 1.70 | 1.35 | 1.34 | 1.38 | 1.33 |
| **4** | 0.39 | 2.10 | 1.65 | 1.61 | 1.65 | 1.60 |
| **5** | 0.42 | 2.51 | 1.94 | 1.96 | 1.98 | 1.89 |
| **6** | 0.44 | 2.98 | 2.24 | 2.26 | 2.32 | 2.20 |
| **7** | 0.47 | 3.33 | 2.54 | 2.57 | 2.67 | 2.50 |
| **8** | 0.50 | 3.73 | 2.82 | 2.89 | 2.94 | 2.79 |
| **9** | 0.52 | 4.14 | 3.12 | 3.17 | 3.22 | 3.07 |
| **10** | 0.55 | 4.54 | 3.41 | 3.44 | 3.50 | 3.33 |
| **11** | 0.58 | 4.94 | 3.70 | 3.73 | 3.80 | 3.57 |
| **12** | 0.62 | 5.39 | 4.05 | 3.99 | 4.06 | 3.84 |
| **13** | 0.63 | 5.76 | 4.28 | 4.33 | 4.37 | 4.13 |
| **14** | 0.66 | 6.15 | 4.58 | 4.59 | 4.73 | 4.42 |
| **15** | 0.69 | 6.55 | 4.89 | 4.89 | 4.97 | 4.71 |
| **16** | 0.71 | 6.79 | 5.04 | 5.07 | 5.13 | 4.83 |

We can conclude drom this that at the desired mixing rate of 16000Hz, the multiplcation path for the 68060/50MHz would consume a maximum of 9.7% of the target with all 16 channels playing. For the 68040/40MHz, the Delta LUT Pre Encoded path would require almost 25%. We can project from this that at 25MHz, assumung linear scaling that the the 68040 Delta LUT Pre Encoded path would require 7.73 ms, reaching 38.6%

For the 68040, particularly at lower clock speeds, e.g. 25MHz 8 channels at 16000 Hz is probably the most acceptable.

For slower machines, fewer channels and/or lower mixing rates might still produce a better acousitc result than the original since each input channel has independent left/right volume that can be adjusted as the sound is playing and the output dynamic range is higher than 8-bit..

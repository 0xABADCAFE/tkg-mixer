#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include<stdlib.h>

static int16_t vol_table[512] = { 0 };


void init_vol_table(int16_t scale) {
	printf("Initialising table with scale factor %d per level\n", (int)scale);
	for (int i = -256; i < 256; ++i) {
		vol_table[i + 256] = i * scale;
	}
}

typedef struct {
	int8_t* data;
	size_t  size;
	
	int16_t	delta_16A;
	int16_t delta_16B;
	int16_t integrated_16A;
	int16_t	integrated_16B;
	int16_t min_delta_8;
	int16_t max_delta_8;
	
} Sound;

void load_raw_sample(char const* name, Sound* sound) {
	FILE* f;
	if ( (f = fopen(name, "rb")) ) {
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
		int8_t* buffer = (int8_t*)malloc(size);
		if (buffer) {
			fread(buffer, 1, size, f);
		}		
		fclose(f);
		sound->data = buffer;
		sound->size = size;
		
		printf("Loaded %s [%zu bytes at %p]\n", name, sound->size, sound->data);
	}
}

int main(void) {
	init_vol_table(256);
	
	Sound sound = {
		NULL, // data
		0,    // size		
		0,    // delta_16A - "gold"
		0,    // delta_16B - evaluation
		0,    // integrated_16A - "gold"
		0,    // integrated_16B - evaluation
		1000, // min_delta_8
		-1000 // max_delta_8
	};
	
	load_raw_sample("linear.raw", &sound);
	
	if (sound.data) {
		int16_t const* table   = vol_table + 256;
		int16_t last_sample    = 0;
		int16_t last_sample_16 = 0;
		for (size_t i = 0; i < sound.size; ++i) {
			int16_t sample = (int16_t)sound.data[i];
			
			int16_t sample_16 = table[sample];
	
			sound.delta_16A = sample_16 - last_sample_16;
			last_sample_16 = sample_16;		
			
			sound.integrated_16A += sound.delta_16A;
			
			int16_t delta_8  = sample - last_sample; // difference can be -256 to +255
			last_sample = sample;
			
			if (delta_8 < sound.min_delta_8) {
				sound.min_delta_8 = delta_8;
			}
			if (delta_8 > sound.max_delta_8) {
				sound.max_delta_8 = delta_8;
			}
			
			sound.delta_16B  = table[delta_8];
			
			sound.integrated_16B += sound.delta_16B;
			
			bool error = (sample_16 != sound.integrated_16A || sample_16 != sound.integrated_16B);
			
			if (0 ==( i & 0xFFF)  || error) {
				printf(
					"%4zu: %4d => %6d [A %6d => %6d] [B %6d => %6d]\n",
					i,
					(int)sound.data[i],
					(int)sample_16,
					(int)sound.delta_16A,
					(int)sound.integrated_16A,
					(int)sound.delta_16B,
					(int)sound.integrated_16B
				);
				if (error) {
					break;
				}
			}
		}	
		printf("Tested %zu samples. Min/Max delta_8 %d/%d\n", sound.size, (int)sound.min_delta_8, (int)sound.max_delta_8);
	
		free(sound.data);
	}	
}

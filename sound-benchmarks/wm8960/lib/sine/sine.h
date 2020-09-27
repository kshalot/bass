#include <Arduino.h>
#include <driver/i2s.h>

#ifndef SINE_H
#define SINE_H

#ifdef __cplusplus
extern "C"
{
#endif

#define I2S_COMM_FORMAT_STAND_MSB 0x03

#define SAMPLE_RATE (36000)
#define I2S_NUM (0)
#define WAVE_FREQ_HZ (100)
#define I2S_BCK_IO (GPIO_NUM_13)
#define I2S_WS_IO (GPIO_NUM_15)
#define I2S_DO_IO (GPIO_NUM_17)
#define I2S_DI_IO (-1)

#define SAMPLE_PER_CYCLE (SAMPLE_RATE / WAVE_FREQ_HZ)

  void setup_triangle_sine_waves(int bits);

  void init_i2s();

#ifdef __cplusplus
}
#endif

#endif
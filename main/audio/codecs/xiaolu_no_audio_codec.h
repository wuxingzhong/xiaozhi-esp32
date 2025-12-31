#ifndef _XIAOLU_NO_AUDIO_CODEC_H
#define _XIAOLU_NO_AUDIO_CODEC_H

#include "no_audio_codec.h"

#include <driver/gpio.h>
#include <driver/i2s_pdm.h>

// 基于修改后的 NoAudioCodecSimplexPdm 实现
// 使用左移5位的 Read 方法实现（修改部分）
// 使用没有 mutex 的 Write 方法实现（修改部分）
class XiaoluNoAudioCodecSimplexPdm : public NoAudioCodec {
public:
    XiaoluNoAudioCodecSimplexPdm(int input_sample_rate, int output_sample_rate, 
                                  gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout, 
                                  gpio_num_t mic_sck, gpio_num_t mic_din);
    int Write(const int16_t* data, int samples) override;
    int Read(int16_t* dest, int samples) override;
};

#endif // _XIAOLU_NO_AUDIO_CODEC_H


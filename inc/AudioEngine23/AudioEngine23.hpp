#pragma once
#include "seeker/common.h"
#include "seeker/loggerApi.h"
#include "AdtsHeader.h"
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavformat/avformat.h>
};

namespace AudioEngine23 {

  enum AudioType {
    AAC, 
    PCMA
  };

  class Decoder {
    uint8_t m_pOutData[1024 * 10];
    const AVCodec* codec;
    AVCodecContext* ctx = NULL;

    bool getData(AVFrame* inFrame, uint8_t*& pOutData, int& iSize) {
      if (!ctx) {
        E_LOG("AVcodecContext is nullptr!");
        return false;
      }
      int ret = avcodec_receive_frame(ctx, inFrame);
      if (ret < 0)
      {
        return false;
      }
      int data_size = av_get_bytes_per_sample(ctx->sample_fmt);
      if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        E_LOG("Failed to calculate data size\n");
        return false;
      }
      int iCopyPos = 0;
      const int channels = 1;
      for (int i = 0; i < inFrame->nb_samples; i++) {
        for (int ch = 0; ch < channels; ch++) {
          memcpy(m_pOutData + iCopyPos, inFrame->data[ch] + data_size * i, data_size);
          iCopyPos = iCopyPos + data_size;
        }
      }
      pOutData = m_pOutData;
      iSize = iCopyPos;
      return true;
    }
  public:
    // 初始化解码器
    int open(int codecType,int sample_rate, AVSampleFormat sample_fmt, int channels) {
      I_LOG("# audio decoder open ar:{}, sf:{}, ch:{}", sample_rate, sample_fmt, channels);
      if(codecType==2)
          codec = avcodec_find_decoder(AV_CODEC_ID_OPUS); //寻找解码器
      else
          codec = avcodec_find_decoder(AV_CODEC_ID_PCM_ALAW); //寻找解码器
      if (!codec) {
        E_LOG("Codec not found\n");
        return -1;
      }
      ctx = avcodec_alloc_context3(codec); //解码器初始化
      if (!ctx) {
        E_LOG("Could not allocate audio codec context\n");
        return -2;
      }
      ctx->sample_fmt = sample_fmt;    //设置采样格式
      ctx->sample_rate = sample_rate;  //设置采样率
      ctx->channels = channels;        //设置通道数
      /* open it */
      if (avcodec_open2(ctx, codec, NULL) < 0) {    //打开解码器+
        E_LOG("Could not open codec\n");
        return -3;
      }
      I_LOG("codec sample_rate = {}, sample_fmt = {}, channels = {}", ctx->sample_rate, ctx->sample_fmt, ctx->channels);
      return 0;
    }

    //解码
    int getFrame(AVPacket* input, AVFrame* output) {
      int ret = 0;
      ret = av_packet_from_data(input, input->data, input->size);
      if (ret < 0)
      {
        E_LOG("av_packet_from_data error");
        av_free(input->data);
        return -1;
      }
      ret = avcodec_send_packet(ctx, input);
      av_packet_unref(input);
      if (ret < 0)
      {
          E_LOG("avcodec_send_packet error");
          return -1;
      }
      while (true) {
        uint8_t* pOutData = NULL;
        int OutSize = 0;
        if (getData(output, pOutData, OutSize)) {
          try {
            if (!ctx) {
              E_LOG("AVcodecContext is nullptr!");
              return -1;
            }
            const int channels = 1;
            int byte_sample = av_get_bytes_per_sample(ctx->sample_fmt);
            int byte = channels * byte_sample;
            if (byte == 0) throw std::logic_error("channels * byte_sample == 0");
            output->nb_samples = OutSize / byte;
          }
          catch (std::exception& ex) {
            E_LOG("ERROR: cal nb samples failed!");
            return -1;
          }
          ret = avcodec_fill_audio_frame(output, ctx->channels, ctx->sample_fmt, pOutData, OutSize, 1);
          if (ret < 0) {
            E_LOG("ERROR: fill audio frame failed!");
            return -1;
          }
          break;
        }
      }
      return 0;
    }

    // 关闭
    void close() {
      I_LOG("decoder closed");
      if (ctx) {
        avcodec_free_context(&ctx);
      }
    }
  };

  class Encoder {
    const AVCodec* codec;
    AVCodecContext* c = NULL;
    int bitrate = 64000;
    int channel = 2;
    int samplerate = 44100;
    int sampleformat = AV_SAMPLE_FMT_S16; //s16le
  public:
    // 初始化编码器
    int open(int codecType,int sample_rate, AVSampleFormat sample_fmt, int channels) {
        if (codecType == 2)
            codec = avcodec_find_encoder(AV_CODEC_ID_OPUS); //寻找解码器
        else
            codec = avcodec_find_encoder(AV_CODEC_ID_PCM_ALAW); //寻找解码器
        if (!codec) {
            E_LOG("ERROR: Codec not found!");
            return -1;
        }

        c = avcodec_alloc_context3(codec);
        if (!c) {
            E_LOG("ERROR: Could not allocate audio codec context");
            return -2;
        }

        /* put sample parameters */
        c->bit_rate = bitrate;

        /* check that the encoder supports s16 pcm input */
        c->sample_fmt = sample_fmt;
        if (!check_sample_fmt(codec, c->sample_fmt)) {
            E_LOG("ERROR: Encoder does not support sample format {}",
                av_get_sample_fmt_name(c->sample_fmt));
            return -3;
        }

        c->sample_rate = sample_rate;
        c->channels = channels;
        if (channels == 1)
            c->channel_layout = AV_CH_LAYOUT_MONO;
        else if (channels == 2)
            c->channel_layout = AV_CH_LAYOUT_STEREO;

        /* open it */
        if (avcodec_open2(c, codec, NULL) < 0) {
             E_LOG("Could not open codec");
            return -4;
        }


        I_LOG("codec sample_rate = {}, sample_fmt = {}, channels = {}", c->sample_rate, c->sample_fmt, c->channels);
        return 0;
    }

    int getPacket(AVFrame* input, AVPacket* output) {
      int ret = 0;
      /* send the frame for encoding */
      ret = avcodec_send_frame(c, input);
      if (ret < 0) {
        E_LOG("Error sending the frame to the encoder");
        return -1;
      }

      /* read all the available output packets (in general there may be any
       number of them */
      ret = avcodec_receive_packet(c, output);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return -2;
      }
      else if (ret < 0) {
        E_LOG("Error encoding audio frame");
        return -3;
      }
      return 0;
    }

    // 关闭
    void close() {
      I_LOG("encoder closed");
      avcodec_free_context(&c);
    }
  private:
    static int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt)
    {
      const enum AVSampleFormat* p = codec->sample_fmts;

      while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
          return 1;
        p++;
      }
      return 0;
    }

  };

	class Demuxer {
  // Variable
  static const int ADTS_HEADER_LENGTH = 7;
  int sample_rate = 0;
  size_t channels = 1;
	public:
    int init(int sample_rate_, size_t channels_) {
      sample_rate = sample_rate_;
      channels = channels_;
      I_LOG("init Muxer success! sr = {}, cnl = {}", sample_rate, channels);
      return 0;
    }
		int demux(std::vector<uint8_t> input, std::deque<std::vector<uint8_t>>& output) {
			/*
        TODO
        [ ] 解析AU_HEADER_LENGTH
        [ ] 解析AU_HEADER
        [ ] 取出AU数据
        [ ] 添加ADTS头
      */
      int aUHeadersLength = ((input[0] & 0xFF) << 16) + (input[1] & 0xFF);
      int auCount = aUHeadersLength / 16;					//AAC帧的数量
      int index = 2 + auCount * 2;							  //上一次解析到的位置
      std::deque<std::vector<uint8_t>> aac_data; 	//ADTS帧列表

      for (int i = 2; i < 2 + 2 * auCount; i += 2) {			//遍历所有auHeader
        int aacDataLen = ((input[i] & 0xFF) << 5) + ((input[i + 1] & 0xF8) >> 3); 		//AAC的数据长度(不包括header)
        std::vector<uint8_t> aacData;											//AAC的数据
        uint8_t* adtsHeader = new uint8_t[7];
        writeAdtsHeaders(adtsHeader, aacDataLen);
        for (int k = 0; k < 7; k++) {
          aacData.push_back(adtsHeader[k]);
        }
        for (int j = index; j < (index + aacDataLen); j++) {
          aacData.push_back(input[j]);
        }
        output.emplace_back(aacData);
        aacData.clear();
        delete[] adtsHeader;
        index += aacDataLen;
      }
      return 0;
		}
	private:
    int writeAdtsHeaders(uint8_t* header, int dataLength) {
      uint8_t profile = 0x02;  // AAC LC
      uint8_t channelCfg = channels;
      uint32_t packetLength = dataLength + 7;
      uint8_t freqIdx;  // 22.05 KHz

      switch (sample_rate) {
      case 96000:
        freqIdx = 0x00;
        break;
      case 88200:
        freqIdx = 0x01;
        break;
      case 64000:
        freqIdx = 0x02;
        break;
      case 48000:
        freqIdx = 0x03;
        break;
      case 44100:
        freqIdx = 0x04;
        break;
      case 32000:
        freqIdx = 0x05;
        break;
      case 24000:
        freqIdx = 0x06;
        break;
      case 22050:
        freqIdx = 0x07;
        break;
      case 16000:
        freqIdx = 0x08;
        break;
      case 12000:
        freqIdx = 0x09;
        break;
      case 11025:
        freqIdx = 0x0A;
        break;
      case 8000:
        freqIdx = 0x0B;
        break;
      case 7350:
        freqIdx = 0x0C;
        break;
      default:
        W_LOG("addADTStoPacket: unsupported sampleRate: " +
          std::to_string(sample_rate));
      }

      //I_LOG("profile={} channelCfg={} packetLength={} freqIdx={}", (int)profile, (int)channelCfg, (int)packetLength, (int)freqIdx);

      // fill in ADTS data
      header[0] = (uint8_t)0xFF;
      header[1] = (uint8_t)0xF1;

      header[2] = (uint8_t)(((profile - 1) << 6) + (freqIdx << 2) + (channelCfg >> 2));
      header[3] = (uint8_t)(((channelCfg & 3) << 6) + (packetLength >> 11));
      header[4] = (uint8_t)((packetLength & 0x07FF) >> 3);
      header[5] = (uint8_t)(((packetLength & 0x0007) << 5) + 0x1F);
      header[6] = (uint8_t)0xFC;

      return ADTS_HEADER_LENGTH;
    }
	};

  class Muxer {


  // API
  public:
  //set sample_rate,channels

    int mux(std::vector<uint8_t> input, std::vector<uint8_t>& output) {
      /*
        TODO
        [ ] 得到AAC数据(带ADTS头)
        [ ] 去除ADTS头
        [ ] 添加AU_HEADER
        [ ] 添加AU_HEADER_LENGTH
      */

      // vector<uint8_t> 转 uint8_t*
      uint8_t* input_ = input.data();

      //I_LOG("input.size 1 = {}", input.size());

      uint8_t* output_ = new uint8_t[1000];
      
      int aac_frame_size = removeAdtsHeaders(input_, output_);
      I_LOG("size = {}, size 2 = {}", aac_frame_size, input.size());

      output = std::vector<uint8_t>(output_ + 7, output_ + aac_frame_size);
      //printHex(input_, input.size());
      //printHex(output_, output.size());

      uint8_t au_header_length_h8 = (output.size() & 0x1FE0) >> 5;
      uint8_t au_header_length_l5 = (output.size() & 0x1F) << 3;

      output.insert(output.begin(), { 0x00, 0x10, au_header_length_h8, au_header_length_l5 });
      //I_LOG("output size = {}", output.size());

      delete[] output_;
      return 0;
    }

  // Self use functions
  private:
    int removeAdtsHeaders(uint8_t* input, uint8_t* output) {
      adts_header_t* adts = (adts_header_t*)input;
      //printHex(input);
      if (adts->syncword_0_to_8 != 0xff ||
        adts->syncword_9_to_12 != 0xf) {
        W_LOG("demux adts fail, adts header wrong!");
        return 0;
      }

      int aac_frame_size = adts->frame_length_0_to_1 << 11 |
        adts->frame_length_2_to_9 << 3 |
        adts->frame_length_10_to_12;
      memcpy(output, input, aac_frame_size);


      return aac_frame_size;
    }

    //增加AU头及LENGTH
    int writeAuHeaders(std::vector<uint8_t>& input) {
      uint16_t size = input.size();
      return 0;
    };

    void printHex(uint8_t* input, int size) {
      std::string code_str;
      for (int i = 0; i < size; i++) {
        //分离16进制数的"十位"和“个位”
        char s1 = char(input[i] >> 4);
        char s2 = char(input[i] & 0xf);
        //将分离得到的数字转换成对应的ASCII码，数字和字母分开，统一按照小写处理
        s1 > 9 ? s1 += 87 : s1 += 48;
        s2 > 9 ? s2 += 87 : s2 += 48;
        //将处理好的字符放入到string中
        code_str.append(1, s1);
        code_str.append(1, s2);
      }
      I_LOG("{}, size = {}", code_str, strlen(code_str.c_str()) / 2);
    }

  };

}

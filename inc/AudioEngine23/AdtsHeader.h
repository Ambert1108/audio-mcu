/**
 * Copyright (c) 2020, SeekLoud Team.
 * Date: 2020/7/4
        * Main Developer: 郝雪
        * Developer:  邱林辉 郭宇
 * Description: adts aac头解析工具
 * Ref: https://wiki.multimedia.cx/index.php?title=ADTS ; https://www.jianshu.com/p/b5ca697535bd
 */

#ifndef AUDIOENGINEIMP5_ADTSHEADER_H
#define AUDIOENGINEIMP5_ADTSHEADER_H

class adts_header_t {
public:
    unsigned char syncword_0_to_8 : 8;

    unsigned char protection_absent : 1;
    unsigned char layer : 2;
    unsigned char ID : 1;
    unsigned char syncword_9_to_12 : 4;

    unsigned char channel_configuration_0_bit : 1; // number of channels
    unsigned char private_bit : 1;
    unsigned char sampling_frequency_index : 4; // sample rate
    unsigned char profile : 2;

    unsigned char frame_length_0_to_1 : 2;
    unsigned char copyrignt_identification_start : 1;
    unsigned char copyright_identification_bit : 1;
    unsigned char home : 1;
    unsigned char original_or_copy : 1;
    unsigned char channel_configuration_1_to_2 : 2;

    unsigned char frame_length_2_to_9 : 8;

    unsigned char adts_buffer_fullness_0_to_4 : 5;
    unsigned char frame_length_10_to_12 : 3;

    unsigned char number_of_raw_data_blocks_in_frame : 2;
    unsigned char adts_buffer_fullness_5_to_10 : 6;
};

#endif //AUDIOENGINEIMP5_ADTSHEADER_H

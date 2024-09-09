#include "AudioPorcessChnl.h"

namespace aom {
	int AudioPorcessChnl::setDemuxer() {
		//try {
		//	if (demuxer) {
		//		demuxer->close();
		//		W_LOG("[MPU::Warn][{}] Demuxer already exists, resetting...", mediaContext->jobId);
		//	}
		//	else {
		//		demuxer = std::make_unique<Demux23>();
		//	}
		//	I_LOG("[DEBUG] start demuxer init, file is {}", mediaContext->mediaFilePath);
		//	if (!mediaContext->hwCtx) throw std::runtime_error("hwCtx is empty, set demuxer failed");
		//	demuxer->init(mediaContext->mediaFilePath.c_str(), mediaContext->hwCtx);
		//	I_LOG("[DEBUG] demuxer init finish");
		//
		//	I_LOG("[MPU::setDemuxer][{}] Demuxer opened succes, file path {}",
		//		mediaContext->jobId, mediaContext->mediaFilePath);
		//}
		//catch (std::exception& ex) {
		//	E_LOG("[MPU::Error][{}] get exception: {}", mediaContext->jobId, ex.what());
		//	return -1;
		//}
		return 0;
	}

	int AudioPorcessChnl::setMuxer() {
		//try {
		//	if (muxer) {
		//		W_LOG("LocalVideo::setMuxer: Job muxer has running...");
		//	}
		//	else {
		//		muxer = std::make_unique<Mux23>();
		//		I_LOG("LocalVideo::setMuxer: Muxer opened successfully.");
		//	}
		//}
		//catch (std::exception& ex) {
		//	E_LOG("[MPU::Error][{}] get exception: {}", mediaContext->jobId, ex.what());
		//	return -1;
		//}
		return 0;
	}

	int AudioPorcessChnl::setDecoder() {
		//try {
		//	if (decoder) {
		//		decoder->close();
		//		W_LOG("[MPU::Warn][{}] Decoder already exists, resetting...", mediaContext->jobId);
		//	}
		//	else {
		//		decoder = std::make_unique<Decoder23>();
		//	}
		//	if (!mediaContext->hwCtx) throw std::invalid_argument("hwCtx is nullptr");
		//	decoder->setCodecContext(ctx);
		//	decoder->open();
		//	I_LOG("[MPU::setDecoder][{}] Decoder opened success", mediaContext->jobId);
		//}
		//catch (std::exception& ex) {
		//	E_LOG("[MPU::Error][{}] get exception: {}", mediaContext->jobId, ex.what());
		//	return -1;
		//}
		return 0;
	}

	int AudioPorcessChnl::setEncoder() {
		//try {
		//	if (encoder) {
		//		encoder->close();
		//		W_LOG("[MPU::Warn][{}] Encoder already exists, resetting...", mediaContext->jobId);
		//	}
		//	else encoder = std::make_unique<Encoder23>();
		//	AVDictionary* dict = nullptr;
		//	encoder->enableHwDevice(mediaContext->hwCtx, AV_PIX_FMT_CUDA);
		//	encoder->rateControlPreset(mediaContext->bitrate * 1000);
		//	auto ctx = encoder->getContext();
		//	ctx->width = width;
		//	ctx->height = height;
		//	ctx->time_base = { 1, mediaContext->framerate };
		//	ctx->framerate = { mediaContext->framerate, 1 };
		//	ctx->pix_fmt = AV_PIX_FMT_CUDA;
		//	ctx->gop_size = mediaContext->gop;
		//	ctx->profile = FF_PROFILE_H264_BASELINE;
		//	ctx->level = 31;
		//	ctx->max_b_frames = 0;
		//	encoder->open(dict);
		//	I_LOG("[MPU::setEncoder][{}] width={}, height={}, bitrate={}, maxBitrate={}, "
		//		"bufsize={}, framemate={}, gop={}, fmt={}, profile={}, level={}, maxBFrame={}, qmax={}, qmin={}",
		//		mediaContext->jobId, ctx->width, ctx->height, ctx->bit_rate, ctx->rc_max_rate, ctx->rc_buffer_size,
		//		ctx->framerate.num / ctx->framerate.den, ctx->gop_size, ctx->pix_fmt, ctx->profile,
		//		ctx->level, ctx->max_b_frames, ctx->qmax, ctx->qmin);
		//	ctx = nullptr;
		//	I_LOG("[MPU::setEncoder][{}] Encoder opened success.", mediaContext->jobId);
		//}
		//catch (std::exception& ex) {
		//	E_LOG("[MPU::Error][{}] get exception: {}", mediaContext->jobId, ex.what());
		//	return -1;
		//}
		return 0;
	}
}
#include "avutility.h"

#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/defs.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>

void av_frame_dump(FILE* fp, struct AVFrame* frame) {
	if (frame->nb_samples != 0) {
		fprintf(fp, "nb_samples=%d\n", frame->nb_samples);
		fprintf(fp, "sample_fmt=%s\n", av_get_sample_fmt_name(frame->format));
		fprintf(fp, "sample_rate=%d\n", frame->sample_rate);
		char ch_layout[64];
		av_channel_layout_describe(&frame->ch_layout, ch_layout, sizeof(ch_layout));
		fprintf(fp, "ch_layout=%s\n", ch_layout);
	} else {
		fprintf(fp, "width=%d\n", frame->width);
		fprintf(fp, "height=%d\n", frame->height);
		fprintf(fp, "pix_fmt=%s\n", av_get_pix_fmt_name(frame->format));
	}
	fprintf(fp, "time_base=%d/%d\n", frame->time_base.num, frame->time_base.den);
	fprintf(fp, "pts=%ld\n", frame->pts);
	fprintf(fp, "pts_time=%lf\n", frame->pts * av_q2d(frame->time_base));
	fprintf(fp, "dts=%ld\n", frame->pkt_dts);
	fprintf(fp, "dts_time=%lf\n", frame->pkt_dts * av_q2d(frame->time_base));
}

size_t av_packet_serialize(uint8_t* buffer, struct AVPacket* pkt) {
	size_t size = 2 * sizeof(int64_t) + 3 * sizeof(int) + pkt->size;
	for (int i = 0; i < pkt->side_data_elems; ++i)
		size += sizeof(enum AVPacketSideDataType) + sizeof(size_t) + pkt->side_data[i].size;
	if (buffer != NULL) {
		memcpy(buffer, &pkt->size, sizeof(int));
		buffer += sizeof(int);
		memcpy(buffer, &pkt->pts, sizeof(int64_t));
		buffer += sizeof(int64_t);
		memcpy(buffer, &pkt->dts, sizeof(int64_t));
		buffer += sizeof(int64_t);
		memcpy(buffer, &pkt->flags, sizeof(int));
		buffer += sizeof(int);
		memcpy(buffer, &pkt->side_data_elems, sizeof(int));
		buffer += sizeof(int);
		for (int i = 0; i < pkt->side_data_elems; ++i) {
			memcpy(buffer, &pkt->side_data[i].type, sizeof(enum AVPacketSideDataType));
			buffer += sizeof(enum AVPacketSideDataType);
			memcpy(buffer, &pkt->side_data[i].size, sizeof(size_t));
			buffer += sizeof(size_t);
			memcpy(buffer, pkt->side_data[i].data, pkt->side_data[i].size);
			buffer += pkt->side_data[i].size;
		}
		memcpy(buffer, pkt->data, pkt->size);
	}
	return size;
}

struct AVPacket* av_packet_deserialize(uint8_t* buffer) {
	struct AVPacket* pkt = av_packet_alloc();
	if (!pkt)
		return NULL;
	memcpy(&pkt->size, buffer, sizeof(int));
	buffer += sizeof(int);
	memcpy(&pkt->pts, buffer, sizeof(int64_t));
	buffer += sizeof(int64_t);
	memcpy(&pkt->dts, buffer, sizeof(int64_t));
	buffer += sizeof(int64_t);
	memcpy(&pkt->flags, buffer, sizeof(int));
	buffer += sizeof(int);
	int side_data_elems;
	memcpy(&side_data_elems, buffer, sizeof(int));
	buffer += sizeof(int);
	for (int i = 0; i < side_data_elems; ++i) {
		enum AVPacketSideDataType type;
		memcpy(&type, buffer, sizeof(type));
		buffer += sizeof(type);
		size_t size;
		memcpy(&size, buffer, sizeof(size_t));
		buffer += sizeof(size_t);
		uint8_t* data = av_packet_new_side_data(pkt, type, size);
		if (data == NULL) {
			fprintf(stderr, "failed to allocate packet side data\n");
			av_packet_free(&pkt);
			return NULL;
		}
		memcpy(data, buffer, size);
		buffer += size;
	}
	uint8_t* data = av_malloc(pkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
	if (data == NULL) {
		fprintf(stderr, "failed to allocate packet data buffer\n");
		av_packet_free(&pkt);
		return NULL;
	}
	memcpy(data, buffer, pkt->size);
	memset(data + pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	if (av_packet_from_data(pkt, data, pkt->size) < 0) {
		av_free(data);
		av_packet_free(&pkt);
	}
	return pkt;
}

static bool check_audio_coder(const AVCodec* codec,
                              enum AVSampleFormat sample_fmt,
                              int sample_rate,
                              int nb_channels,
                              int frame_size) {
	// deprecated ways for checking supported codec params
	// bool found = false;
	// for (const enum AVSampleFormat* fmt = codec->sample_fmts; *fmt != AV_SAMPLE_FMT_NONE; ++fmt)
	// { 	if (*fmt == sample_fmt) { 		found = true; 		break;
	//	}
	// }
	// if (!found) {
	//	fprintf(stderr,
	//	        "sample format %s not supported by %s codec\n",
	//	        av_get_sample_fmt_name(sample_fmt),
	//	        codec->name);
	//	return false;
	// }
	// found = false;
	// for (const int* rate = codec->supported_samplerates; *rate != 0; ++rate) {
	//	if (*rate == sample_rate) {
	//		found = true;
	//		break;
	//	}
	// }
	// if (!found) {
	//	fprintf(stderr, "sample rate %d not supported by %s codec\n", sample_rate, codec->name);
	//	return false;
	// }
	// found = false;
	// for (const struct AVChannelLayout* layout = codec->ch_layouts; layout->nb_channels != 0;
	//      layout++) {
	//	if (layout->nb_channels == nb_channels) {
	//		found = true;
	//		break;
	//	}
	// }
	// if (!found) {
	//	fprintf(stderr, "%d channels not supported by %s codec\n", nb_channels, codec->name);
	//	return false;
	// }
	AVCodecContext* ctx = avcodec_alloc_context3(codec);
	ctx->sample_fmt     = sample_fmt;
	ctx->sample_rate    = sample_rate;
	ctx->frame_size     = frame_size;
	ctx->time_base      = av_make_q(1, 1000);
	av_channel_layout_default(&ctx->ch_layout, nb_channels);
	int ret = avcodec_open2(ctx, codec, NULL);
	if (ret < 0)
		fprintf(stderr,
		        "failed to open %s %s: ret = %d\n",
		        codec->name,
		        av_codec_is_encoder(codec) ? "encoder" : "decoder",
		        ret);
	else {
		if (ctx->sample_fmt != sample_fmt) {
			fprintf(stderr,
			        "%s %s changed sample format: %s -> %s\n",
			        codec->name,
			        av_codec_is_encoder(codec) ? "encoder" : "decoder",
			        av_get_sample_fmt_name(sample_fmt),
			        av_get_sample_fmt_name(ctx->sample_fmt));
			ret = -1;
		}
		if (ctx->sample_rate != sample_rate) {
			fprintf(stderr,
			        "%s %s changed sample rate: %d -> %d\n",
			        codec->name,
			        av_codec_is_encoder(codec) ? "encoder" : "decoder",
			        sample_rate,
			        ctx->sample_rate);
			ret = -1;
		}
		if (ctx->frame_size != frame_size) {
			fprintf(stderr,
			        "%s %s changed frame size: %d -> %d\n",
			        codec->name,
			        av_codec_is_encoder(codec) ? "encoder" : "decoder",
			        frame_size,
			        ctx->frame_size);
			ret = -1;
		}
		avcodec_free_context(&ctx);
	}
	return ret == 0;
}

static bool check_video_coder(
    const AVCodec* codec, enum AVPixelFormat pixel_fmt, int width, int height, int framerate) {
	AVCodecContext* ctx = avcodec_alloc_context3(codec);
	ctx->pix_fmt        = pixel_fmt;
	ctx->width          = width;
	ctx->height         = height;
	ctx->framerate      = av_make_q(1, framerate);
	ctx->time_base      = av_make_q(1, 1000);
	int ret             = avcodec_open2(ctx, codec, NULL);
	if (ret < 0)
		fprintf(stderr,
		        "failed to open %s %s: ret = %d\n",
		        codec->name,
		        av_codec_is_encoder(codec) ? "encoder" : "decoder",
		        ret);
	else {
		if (ctx->pix_fmt != pixel_fmt) {
			fprintf(stderr,
			        "%s %s changed pixel format: %s -> %s\n",
			        codec->name,
			        av_codec_is_encoder(codec) ? "encoder" : "decoder",
			        av_get_pix_fmt_name(pixel_fmt),
			        av_get_pix_fmt_name(ctx->pix_fmt));
			ret = -1;
		}
		if (ctx->width != width || ctx->height != height) {
			fprintf(stderr,
			        "%s %s changed resolution: %dx%d -> %dx%d\n",
			        codec->name,
			        av_codec_is_encoder(codec) ? "encoder" : "decoder",
			        width,
			        height,
			        ctx->width,
			        ctx->height);
			ret = -1;
		}
		if (ctx->framerate.num * framerate != ctx->framerate.den) {
			fprintf(stderr,
			        "%s %s changed frame rate: 1/%d -> %d/%d\n",
			        codec->name,
			        av_codec_is_encoder(codec) ? "encoder" : "decoder",
			        framerate,
			        ctx->framerate.num,
			        ctx->framerate.den);
			ret = -1;
		}
		avcodec_free_context(&ctx);
	}
	return ret == 0;
}

bool check_audio_codec(enum AVCodecID id,
                       enum AVSampleFormat sample_fmt,
                       int sample_rate,
                       int nb_channels,
                       int frame_size) {
	const AVCodec* encoder = avcodec_find_encoder(id);
	if (encoder == NULL) {
		fprintf(stderr, "encoder for %s not found\n", avcodec_get_name(id));
		return false;
	}
	const AVCodec* decoder = avcodec_find_decoder(id);
	if (decoder == NULL) {
		fprintf(stderr, "decoder for %s not found\n", avcodec_get_name(id));
		return false;
	}
	return check_audio_coder(encoder, sample_fmt, sample_rate, nb_channels, frame_size) &&
	       check_audio_coder(decoder, sample_fmt, sample_rate, nb_channels, frame_size);
}

bool check_video_codec(
    enum AVCodecID id, enum AVPixelFormat pixel_fmt, int width, int height, int framerate) {
	const AVCodec* encoder = avcodec_find_encoder(id);
	if (encoder == NULL) {
		fprintf(stderr, "encoder for %s not found\n", avcodec_get_name(id));
		return false;
	}
	const AVCodec* decoder = avcodec_find_decoder(id);
	if (decoder == NULL) {
		fprintf(stderr, "decoder for %s not found\n", avcodec_get_name(id));
		return false;
	}
	return check_video_coder(encoder, pixel_fmt, width, height, framerate) &&
	       check_video_coder(decoder, pixel_fmt, width, height, framerate);
}

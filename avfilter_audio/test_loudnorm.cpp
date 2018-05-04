extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		printf("Usage: %s input output\n", argv[0]);
		exit(1);
	}

	av_register_all();
	avfilter_register_all();

	/// Input

	// Demuxer

	AVFormatContext *input_format_context = NULL;
	int ret;

	if ((ret = avformat_open_input(&input_format_context, argv[1], NULL, NULL)) < 0) {
		printf("Cannot open input file\n");
		return ret;
	}

	if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
		printf("Cannot find stream information\n");
		return ret;
	}

	// Decoder

	AVCodec *decoder;
	ret = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);

	if (ret < 0) {
		printf("Cannot find an audio stream in the input file\n");
		return ret;
	}

	int input_audio_stream_index = ret;
	AVCodecContext *decoding_context = avcodec_alloc_context3(decoder);

	if (!decoding_context)
		return AVERROR(ENOMEM);

	avcodec_parameters_to_context(decoding_context, input_format_context->streams[input_audio_stream_index]->codecpar);

	if ((ret = avcodec_open2(decoding_context, decoder, NULL)) < 0) {
		printf("Cannot open audio decoder\n");
		return ret;
	}

	/// Init filtering graph

	char args[512];
	AVFilterGraph *filter_graph = avfilter_graph_alloc();

	// Source buffer

	AVRational time_base = input_format_context->streams[input_audio_stream_index]->time_base;
	AVFilter *abuffer = avfilter_get_by_name("abuffer");
	AVFilterContext *abuffer_ctx;

	snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%x",
		time_base.num, time_base.den, decoding_context->sample_rate,
		av_get_sample_fmt_name(decoding_context->sample_fmt), decoding_context->channel_layout);

	ret = avfilter_graph_create_filter(&abuffer_ctx, abuffer, "in", args, NULL, filter_graph);

	if (ret < 0) {
		printf("Cannot create audio buffer source\n");
		exit(1);
	}

	// Sink buffer

	AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
	AVFilterContext *abuffersink_ctx;

	ret = avfilter_graph_create_filter(&abuffersink_ctx, abuffersink, "out", NULL, NULL, filter_graph);

	if (ret < 0) {
		printf("Cannot create audio buffer sink\n");
		exit(1);
	}

	static const enum AVSampleFormat out_sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
	ret = av_opt_set_int_list(abuffersink_ctx, "sample_fmts", out_sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		printf("Cannot set output sample format\n");
		exit(1);
	}

	static const int64_t out_channel_layouts[] = { decoding_context->channel_layout, -1 };
	ret = av_opt_set_int_list(abuffersink_ctx, "channel_layouts", out_channel_layouts, -1, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		printf("Cannot set output channel layout\n");
		exit(1);
	}

	static const int out_sample_rates[] = { decoding_context->sample_rate, -1 };
	ret = av_opt_set_int_list(abuffersink_ctx, "sample_rates", out_sample_rates, -1, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		printf("Cannot set output sample rate\n");
		exit(1);
	}

	// Loudness filter

	AVFilter *loudnorm = avfilter_get_by_name("loudnorm");
	AVFilterContext *loudnorm_ctx = NULL;

	snprintf(args, sizeof(args), "I=-16:TP=-1.5:LRA=11");
	printf("loudnorm: %s\n", args);
	ret = avfilter_graph_create_filter(&loudnorm_ctx, loudnorm, NULL, args, NULL, filter_graph);
	if (ret < 0) {
		printf("error initializing loudnorm filter\n");
		exit(ret);
	}

	// Connecting graph

	if (ret >= 0) ret = avfilter_link(abuffer_ctx, 0, loudnorm_ctx, 0);
	if (ret >= 0) ret = avfilter_link(loudnorm_ctx, 0, abuffersink_ctx, 0);

	if (ret < 0) {
		printf("error connecting filters\n");
		return ret;
	}

	if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0) {
		printf("error configuring graph\n");
		exit(0);
	}

	/// Output

	// Encoder

	char *output_filename = argv[2];

	AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
	if (!encoder) {
		printf("Codec not found\n");
		exit(1);
	}

	AVCodecContext *encoding_context = avcodec_alloc_context3(encoder);
	if (!encoding_context) {
		printf("Could not allocate encoder context\n");
		exit(1);
	}

	encoding_context->sample_rate = decoding_context->sample_rate;
	encoding_context->channel_layout = decoding_context->channel_layout;
	encoding_context->channels = av_get_channel_layout_nb_channels(decoding_context->channel_layout);
	encoding_context->sample_fmt = AV_SAMPLE_FMT_S16;

	if (avcodec_open2(encoding_context, encoder, NULL) < 0) {
		printf("Could not open codec\n");
		exit(1);
	}

	// Muxer

	AVFormatContext *output_format_context = NULL;
	avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_filename);

	AVStream *out_stream = avformat_new_stream(output_format_context, NULL);
	avcodec_parameters_from_context(out_stream->codecpar, encoding_context);

	av_dump_format(output_format_context, 0, output_filename, 1);

	if (!(output_format_context->flags & AVFMT_NOFILE)) {
		if (avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
			printf("Could not open '%s'\n", output_filename);
			exit(1);
		}
	}

	ret = avformat_write_header(output_format_context, NULL);
	if (ret < 0) {
		printf("Could not write header\n");
		exit(0);
	}

	/// Main loop

	AVPacket *input_packet = av_packet_alloc();
	AVFrame *input_frame = av_frame_alloc();
	AVFrame *filtered_frame = av_frame_alloc();
	AVPacket *output_packet = av_packet_alloc();

	// Read packets from the container
	while (1) {
		ret = av_read_frame(input_format_context, input_packet);
		if (ret < 0) {
			break;
		}

		if (input_packet->stream_index == input_audio_stream_index)	{
			// Push packet to the decoder
			ret = avcodec_send_packet(decoding_context, input_packet);
			if (ret < 0) {
				printf("Error while sending a input_packet to the decoder\n");
				break;
			}

			// Pull frames from the decoder
			while (ret >= 0) {
				ret = avcodec_receive_frame(decoding_context, input_frame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				} else if (ret < 0)	{
					printf("Error while receiving a input_frame from the decoder\n");
					exit(0);
				}

				// Push frames to the filtergraph
				if (av_buffersrc_add_frame_flags(abuffer_ctx, input_frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)	{
					printf("Error while feeding the audio filtergraph\n");
					break;
				}
				printf("add_frame: %d\n", input_frame->nb_samples);
				// Pull frames from the filtergraph
				while (1) {
					ret = av_buffersink_get_frame(abuffersink_ctx, filtered_frame);

					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
						break;
					}
					printf("get_frame: %d\n", filtered_frame->nb_samples);
					if (ret < 0) {
						printf("Error while pulling frames form the filtergraph\n");
						exit(1);
					}

					// Push frames to the encoder
					ret = avcodec_send_frame(encoding_context, filtered_frame);

					if (ret < 0) {
						printf("Error sending the frame to the encoder\n");
						exit(1);
					}

					// Pull packets from the encoder
					while (ret >= 0) {
						ret = avcodec_receive_packet(encoding_context, output_packet);

						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
							break;
						}

						if (ret < 0) {
							printf("Error encoding audio input_frame\n");
							exit(1);
						}

						av_write_frame(output_format_context, output_packet);

						av_packet_unref(output_packet);
					}
					av_frame_unref(filtered_frame);
				}
				av_frame_unref(input_frame);
			}
		}
		av_packet_unref(input_packet);
	}

	// Handle errors
	if (ret < 0 && ret != AVERROR_EOF) {
		printf("Error occurred: %s\n", av_err2str(ret));
		exit(1);
	}

	// Finish file
	av_write_trailer(output_format_context);

	if (!(output_format_context->flags & AVFMT_NOFILE)) {
		if (avio_close(output_format_context->pb) < 0) {
			printf("Could not close '%s'\n", output_filename);
			exit(1);
		}
	}

	/// Cleanup

	// Input
	avformat_close_input(&input_format_context);
	avcodec_free_context(&decoding_context);

	// Graph
	avfilter_graph_free(&filter_graph);

	// Packets, frames
	av_frame_free(&input_frame);
	av_frame_free(&filtered_frame);
	av_packet_free(&input_packet);
	av_packet_free(&output_packet);

	// Output
	avcodec_free_context(&encoding_context);
	avformat_free_context(output_format_context);

	return 0;
}

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

int main(int argc, char* argv)
{
	int ret = 0;

	// input yuv
	FILE* inFile = NULL;
	const char* inFileName = "sintel_480x272_yuv420p.yuv";
	fopen_s(&inFile, inFileName, "rb+");
	if (!inFile) {
		printf("Fail to open file\n");
		return -1;
	}

	int in_width = 480;
	int in_height = 272;

	// output yuv
	FILE* outFile = NULL;
	const char* outFileName = "out_crop_vfilter.yuv";
	fopen_s(&outFile, outFileName, "wb");
	if (!outFile) {
		printf("Fail to create file for output\n");
		return -1;
	}

	avfilter_register_all();

	AVFilterGraph* filter_graph = avfilter_graph_alloc();
	if (!filter_graph) {
		printf("Fail to create filter graph!\n");
		return -1;
	}

	// source filter
	char args[512];
	_snprintf_s(args, sizeof(args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		in_width, in_height, AV_PIX_FMT_YUV420P,
		1, 25, 1, 1);
	AVFilter* bufferSrc = avfilter_get_by_name("buffer");
	AVFilterContext* bufferSrc_ctx;
	ret = avfilter_graph_create_filter(&bufferSrc_ctx, bufferSrc, "in", args, NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create filter bufferSrc\n");
		return -1;
	}

	// sink filter
	AVBufferSinkParams *bufferSink_params;
	AVFilterContext* bufferSink_ctx;
	AVFilter* bufferSink = avfilter_get_by_name("buffersink");
	enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
	bufferSink_params = av_buffersink_params_alloc();
	bufferSink_params->pixel_fmts = pix_fmts;
	ret = avfilter_graph_create_filter(&bufferSink_ctx, bufferSink, "out", NULL, bufferSink_params, filter_graph);
	if (ret < 0) {
		printf("Fail to create filter sink filter\n");
		return -1;
	}

	// split filter
	AVFilter *splitFilter = avfilter_get_by_name("split");
	AVFilterContext *splitFilter_ctx;
	ret = avfilter_graph_create_filter(&splitFilter_ctx, splitFilter, "split", "outputs=2", NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create split filter\n");
		return -1;
	}

	// crop filter
	AVFilter *cropFilter = avfilter_get_by_name("crop");
	AVFilterContext *cropFilter_ctx;
	ret = avfilter_graph_create_filter(&cropFilter_ctx, cropFilter, "crop", "out_w=iw:out_h=ih/2:x=0:y=0", NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create crop filter\n");
		return -1;
	}

	// vflip filter
	AVFilter *vflipFilter = avfilter_get_by_name("vflip");
	AVFilterContext *vflipFilter_ctx;
	ret = avfilter_graph_create_filter(&vflipFilter_ctx, vflipFilter, "vflip", NULL, NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create vflip filter\n");
		return -1;
	}

	// overlay filter
	AVFilter *overlayFilter = avfilter_get_by_name("overlay");
	AVFilterContext *overlayFilter_ctx;
	ret = avfilter_graph_create_filter(&overlayFilter_ctx, overlayFilter, "overlay", "y=0:H/2", NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create overlay filter\n");
		return -1;
	}

	// src filter to split filter
	ret = avfilter_link(bufferSrc_ctx, 0, splitFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link src filter and split filter\n");
		return -1;
	}
	// split filter's first pad to overlay filter's main pad
	ret = avfilter_link(splitFilter_ctx, 0, overlayFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link split filter and overlay filter main pad\n");
		return -1;
	}
	// split filter's second pad to crop filter
	ret = avfilter_link(splitFilter_ctx, 1, cropFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link split filter's second pad and crop filter\n");
		return -1;
	}
	// crop filter to vflip filter
	ret = avfilter_link(cropFilter_ctx, 0, vflipFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link crop filter and vflip filter\n");
		return -1;
	}
	// vflip filter to overlay filter's second pad
	ret = avfilter_link(vflipFilter_ctx, 0, overlayFilter_ctx, 1);
	if (ret != 0) {
		printf("Fail to link vflip filter and overlay filter's second pad\n");
		return -1;
	}
	// overlay filter to sink filter
	ret = avfilter_link(overlayFilter_ctx, 0, bufferSink_ctx, 0);
	if (ret != 0) {
		printf("Fail to link overlay filter and sink filter\n");
		return -1;
	}

	// check filter graph
	ret = avfilter_graph_config(filter_graph, NULL);
	if (ret < 0) {
		printf("Fail in filter graph\n");
		return -1;
	}

	AVFrame *frame_in = av_frame_alloc();
	unsigned char *frame_buffer_in = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, in_width, in_height, 1));
	av_image_fill_arrays(frame_in->data, frame_in->linesize, frame_buffer_in,
		AV_PIX_FMT_YUV420P, in_width, in_height, 1);

	AVFrame *frame_out = av_frame_alloc();
	unsigned char *frame_buffer_out = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, in_width, in_height, 1));
	av_image_fill_arrays(frame_out->data, frame_out->linesize, frame_buffer_out,
		AV_PIX_FMT_YUV420P, in_width, in_height, 1);

	frame_in->width = in_width;
	frame_in->height = in_height;
	frame_in->format = AV_PIX_FMT_YUV420P;

	while (1) {

		if (fread(frame_buffer_in, 1, in_width*in_height * 3 / 2, inFile) != in_width*in_height * 3 / 2) {
			break;
		}
		//input Y,U,V
		frame_in->data[0] = frame_buffer_in;
		frame_in->data[1] = frame_buffer_in + in_width*in_height;
		frame_in->data[2] = frame_buffer_in + in_width*in_height * 5 / 4;

		if (av_buffersrc_add_frame(bufferSrc_ctx, frame_in) < 0) {
			printf("Error while add frame.\n");
			break;
		}

		/* pull filtered pictures from the filtergraph */
		ret = av_buffersink_get_frame(bufferSink_ctx, frame_out);
		if (ret < 0)
			break;

		//output Y,U,V
		if (frame_out->format == AV_PIX_FMT_YUV420P) {
			for (int i = 0; i < frame_out->height; i++) {
				fwrite(frame_out->data[0] + frame_out->linesize[0] * i, 1, frame_out->width, outFile);
			}
			for (int i = 0; i < frame_out->height / 2; i++) {
				fwrite(frame_out->data[1] + frame_out->linesize[1] * i, 1, frame_out->width / 2, outFile);
			}
			for (int i = 0; i < frame_out->height / 2; i++) {
				fwrite(frame_out->data[2] + frame_out->linesize[2] * i, 1, frame_out->width / 2, outFile);
			}
		}
		printf("Process 1 frame!\n");
		av_frame_unref(frame_out);
	}

	fclose(inFile);
	fclose(outFile);

	av_frame_free(&frame_in);
	av_frame_free(&frame_out);
	avfilter_graph_free(&filter_graph);
	return 0;
}
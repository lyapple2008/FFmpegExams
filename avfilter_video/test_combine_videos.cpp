extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

#include <string>
using namespace std;

int main(int argc, char* argv[])
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
	const char* outFileName = "out_combine_2x2.yuv";
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

	// buffer src
	const char* bufferSrcParams = "video_size=480x272:pix_fmt=0:time_base=1/25:pixel_aspect=1/1";
	AVFilter *srcBufferArray[4] = {NULL, NULL, NULL, NULL};
	AVFilterContext *srcBufferCtxArray[4] = { NULL, NULL, NULL, NULL };
	for (int i = 0; i < 4; i++) {
		srcBufferArray[i] = avfilter_get_by_name("buffer");
		string name = "in_" + to_string(i);
		ret = avfilter_graph_create_filter(&(srcBufferCtxArray[i]), srcBufferArray[i], 
									name.c_str(), bufferSrcParams, NULL, filter_graph);
		if (ret < 0) {
			printf("Fail to create filter bufferSrc\n");
			return -1;
		}
	}

	// buffer sink
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

	// pad filter
	AVFilter* padFilter = avfilter_get_by_name("pad");
	AVFilterContext *padFilterCtx;
	ret = avfilter_graph_create_filter(&padFilterCtx, padFilter, "pad", "w=iw*2:h=ih*2", NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create pad filter!\n");
		return -1;
	}

	// overlay filter 1
	AVFilter *overlayFilter_1 = avfilter_get_by_name("overlay");
	AVFilterContext *overlayFilterCtx_1;
	ret = avfilter_graph_create_filter(&overlayFilterCtx_1, overlayFilter_1, "overlay_1", "w", NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create overlay filter - 1\n");
		return -1;
	}

	// overlay filter 2
	AVFilter *overlayFilter_2 = avfilter_get_by_name("overlay");
	AVFilterContext *overlayFilterCtx_2;
	ret = avfilter_graph_create_filter(&overlayFilterCtx_2, overlayFilter_2, "overlay_2", "0:h", NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create overlay filter - 2\n");
		return -1;
	}

	// overlay filter 3
	AVFilter* overlayFilter_3 = avfilter_get_by_name("overlay");
	AVFilterContext *overlayFilterCtx_3;
	ret = avfilter_graph_create_filter(&overlayFilterCtx_3, overlayFilter_3, "overlay_3", "w:h", NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create overlay filter - 3\n");
		return -1;
	}

	// link bufferSrc0 with pad filter
	ret = avfilter_link(srcBufferCtxArray[0], 0, padFilterCtx, 0);

	ret = avfilter_link(padFilterCtx, 0, overlayFilterCtx_1, 0);
	ret = avfilter_link(srcBufferCtxArray[1], 0, overlayFilterCtx_1, 1);

	ret = avfilter_link(overlayFilterCtx_1, 0, overlayFilterCtx_2, 0);
	ret = avfilter_link(srcBufferCtxArray[2], 0, overlayFilterCtx_2, 1);

	ret = avfilter_link(overlayFilterCtx_2, 0, overlayFilterCtx_3, 0);
	ret = avfilter_link(srcBufferCtxArray[3], 0, overlayFilterCtx_3, 1);

	ret = avfilter_link(overlayFilterCtx_3, 0, bufferSink_ctx, 0);

	// check filter graph
	ret = avfilter_graph_config(filter_graph, NULL);
	if (ret < 0) {
		printf("Fail in filter graph\n");
		return -1;
	}

	char *graph_str = avfilter_graph_dump(filter_graph, NULL);
	FILE* graphFile = NULL;
	fopen_s(&graphFile, "graphFile.txt", "w");
	fprintf(graphFile, "%s", graph_str);
	av_free(graph_str);

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

		for (int i = 0; i < 4; i++) {
			ret = av_buffersrc_add_frame(srcBufferCtxArray[i], frame_in);
			if (ret < 0) {
				printf("Error while add frame to source buffer [%d]\n", i);
				break;
			}
		}

		/* pull filtered pictures from the filter graph */
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
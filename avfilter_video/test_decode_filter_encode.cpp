extern "C" 
{
#include <libavformat/avformat.h>
}

int main(int argc, char* argv[])
{
	const char* inFile = "Wildlife.mp4";

	av_register_all();

	AVFormatContext* fmtCtx = NULL;

	if (avformat_open_input(&fmtCtx, inFile, NULL, NULL)) {
		printf("Could not open source file %s\n", inFile);
		return -1;
	}

	if (avformat_find_stream_info(fmtCtx, NULL) < 0) {
		printf("Could not find stream information\n");
		return -1;
	}


}
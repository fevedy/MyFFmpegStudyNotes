#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

#define MAX_PATH 260

const char *filter_descr = "delogo=x=200:y=100:w=410:h=300:show=0";

static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int video_stream_index = 0;

static int init_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            time_base.num, time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}


/**
 * 将AVFrame(YUV420格式)保存为JPEG格式的图片
 *
 * @param width YUV420的宽
 * @param height YUV42的高
 *
 */
int MyWriteJPEG(AVFrame* pFrame, int width1, int height1, int iIndex)
{
	char* out_name = "11111111.jpg";
	printf("11111\n");
	int width = pFrame->width;
    int height = pFrame->height;
	printf("11112, %d-%d\n", width, height);
    AVCodecContext *ipCodeCtx = NULL;


    AVFormatContext *pFormatCtx = avformat_alloc_context();
    // 设置输出文件格式
    pFormatCtx->oformat = av_guess_format("mjpeg", NULL, NULL);

    // 创建并初始化输出AVIOContext
    if (avio_open(&pFormatCtx->pb, out_name, AVIO_FLAG_READ_WRITE) < 0) {
        printf("Couldn't open output file.");
        return -1;
    }

    // 构建一个新stream
    AVStream *pAVStream = avformat_new_stream(pFormatCtx, 0);
    if (pAVStream == NULL) {
        return -1;
    }

    AVCodecParameters *parameters = pAVStream->codecpar;
    parameters->codec_id = pFormatCtx->oformat->video_codec;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->format = AV_PIX_FMT_YUVJ420P;
    parameters->width = pFrame->width;
    parameters->height = pFrame->height;

    AVCodec *pCodec = avcodec_find_encoder(pAVStream->codecpar->codec_id);

    if (!pCodec) {
        printf("Could not find encoder\n");
        return -1;
    }

    ipCodeCtx = avcodec_alloc_context3(pCodec);
    if (!ipCodeCtx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    if ((avcodec_parameters_to_context(ipCodeCtx, pAVStream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return -1;
    }

    ipCodeCtx->time_base = (AVRational) {1, 25};

    if (avcodec_open2(ipCodeCtx, pCodec, NULL) < 0) {
        printf("Could not open codec.");
        return -1;
    }

    int ret = avformat_write_header(pFormatCtx, NULL);
    if (ret < 0) {
        printf("write_header fail\n");
        return -1;
    }

    int y_size = width * height;

    //Encode
    // 给AVPacket分配足够大的空间
    AVPacket pkt;
    av_new_packet(&pkt, y_size * 3);

    printf("zys:::1111111111\n");
    // 编码数据
    ret = avcodec_send_frame(ipCodeCtx, pFrame);
    if (ret < 0) {
        printf("Could not avcodec_send_frame.");
        return -1;
    }

    printf("zys:::1111111112\n");
    // 得到编码后数据
    ret = avcodec_receive_packet(ipCodeCtx, &pkt);
    if (ret < 0) {
        printf("Could not avcodec_receive_packet");
        return -1;
    }

    ret = av_write_frame(pFormatCtx, &pkt);

    if (ret < 0) {
        printf("Could not av_write_frame");
        return -1;
    }

    av_packet_unref(&pkt);

    //Write Trailer
    av_write_trailer(pFormatCtx);


    avcodec_close(ipCodeCtx);
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);

    return 0;
}
static int64_t last_pts = AV_NOPTS_VALUE;
static void display_frame(const AVFrame *frame, AVRational time_base)
{
    int x, y;
    uint8_t *p0, *p;
    int64_t delay;

    if (frame->pts != AV_NOPTS_VALUE) {
        if (last_pts != AV_NOPTS_VALUE) {
            /* sleep roughly the right amount of time;
             * usleep is in microseconds, just like AV_TIME_BASE. */
            delay = av_rescale_q(frame->pts - last_pts,
                                 time_base, AV_TIME_BASE_Q);
            if (delay > 0 && delay < 1000000)
                usleep(delay);
        }
        last_pts = frame->pts;
    }

    /* Trivial ASCII grayscale display. */
    p0 = frame->data[0];
    puts("\033c");
    for (y = 0; y < frame->height; y++) {
        p = p0;
        for (x = 0; x < frame->width; x++)
            putchar(" .-+#"[*(p++) / 52]);
        putchar('\n');
        p0 += frame->linesize[0];
    }
    fflush(stdout);
}


    AVFormatContext *pFormatCtx = NULL;
    AVCodecContext *pCodecCtx = NULL;
int main()
{
    int videoStream = -1;
    AVCodec *pCodec = NULL;
    AVFrame *pFrame, *pFrameRGB;
    struct SwsContext *pSwsCtx;
    const char *filename = "/home/pixel/yszhang/ffmpeg/history_0.h264";
    AVPacket *packet;
    int frameFinished;
    int PictureSize = 0;
    uint8_t *outBuff;
    AVFrame *filt_frame;

    filt_frame = av_frame_alloc();
    if ( !filt_frame) {
        perror("Could not allocate frame");
        exit(1);
    }

    //注册编解码器
   // av_register_all();
    // 初始化网络模块
    avformat_network_init();
    // 分配AVFormatContext
    //pFormatCtx = avformat_alloc_context();

    //打开视频文件
    if( avformat_open_input(&pFormatCtx, filename, NULL, NULL) < 0 ) {
        printf ("av open input file failed!\n");
        exit (1);
    }
    fmt_ctx = pFormatCtx;
    //获取流信息
    if( avformat_find_stream_info(pFormatCtx, NULL) < 0 ) {
        printf ("av find stream info failed!\n");
        exit (1);
    }

    //获取视频流
    for( int i = 0; i < pFormatCtx->nb_streams; i++ ) {
	    AVCodec *codec = avcodec_find_decoder(pFormatCtx->streams[i]->codecpar->codec_id);
        if ( codec->type == AVMEDIA_TYPE_VIDEO ) {
            videoStream = i;
            printf ("find video stream %d!\n", i);
            break;
        }
    }
    if( videoStream == -1 ) {
        printf ("find video stream failed!\n");
        exit (1);
    }

    // 寻找解码器
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);;
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if( pCodec == NULL ) {
        printf ("avcode find decoder failed!\n");
        exit (1);
    }
	dec_ctx = pCodecCtx;

    int result = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
    if (result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        return result;
    }

#if 1
if ((init_filters(filter_descr)) < 0)
{
	printf("init filter failed\n");
}
#endif

    //打开解码器
    if( avcodec_open2(pCodecCtx, pCodec, NULL) < 0 ) {
        printf ("avcode open failed!\n");
        exit (1);
    }

    //为每帧图像分配内存
    pFrame = av_frame_alloc();
    pFrameRGB = av_frame_alloc();
    if( (pFrame == NULL) || (pFrameRGB == NULL) ) {
        printf("avcodec alloc frame failed!\n");
        exit (1);
    }

    // 确定图片尺寸
    PictureSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
    printf ("buffer size is %d,%d-%d!\n", PictureSize,pCodecCtx->width, pCodecCtx->height);

    outBuff = (uint8_t*)av_malloc(PictureSize);
    if( outBuff == NULL ) {
        printf("av malloc failed!\n");
        exit(1);
    }
    av_image_fill_arrays( pFrameRGB->data, pFrameRGB->linesize, outBuff, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);

    //设置图像转换上下文
    pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P,
        SWS_BICUBIC, NULL, NULL, NULL);

        packet = av_packet_alloc();
    if (!packet) {
        av_log(NULL, AV_LOG_ERROR, "Cannot allocate packet\n");
        return AVERROR(ENOMEM);
    }


    int i = 0;
    while( av_read_frame(pFormatCtx, packet) >= 0 ) {
        if( packet->stream_index == videoStream ) {
           //avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
	    avcodec_send_packet(pCodecCtx, packet);
	    frameFinished =avcodec_receive_frame(pCodecCtx, pFrame);

            if( !frameFinished ) {
                // 保存为jpeg时不需要反转图像

	     pFrame->pts = pFrame->best_effort_timestamp;	
		
	/* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    int ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        printf("zys:::err here\n");
                       // display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
                	static bool b1 = true;
                	if( b1 ) {
					FILE *fp_yuv=fopen("test.yuv","wb+");
                    if (filt_frame->format==AV_PIX_FMT_YUV420P) {
						//Y, U, V
						for(int i=0;i<filt_frame->height;i++){
							fwrite(filt_frame->data[0]+filt_frame->linesize[0]*i,1,filt_frame->width,fp_yuv);
						}
						for(int i=0;i<filt_frame->height/2;i++){
							fwrite(filt_frame->data[1]+filt_frame->linesize[1]*i,1,filt_frame->width/2,fp_yuv);
						}
						for(int i=0;i<filt_frame->height/2;i++){
							fwrite(filt_frame->data[2]+filt_frame->linesize[2]*i,1,filt_frame->width/2,fp_yuv);
						}
		    }
		    else
		    {
		    printf("zys::yuv is not right%d\n",filt_frame->format);
		    }
                	    MyWriteJPEG(filt_frame, 0, 0, i ++);
                	    b1 = false;
                	}
                    av_frame_unref(filt_frame);
                }
            }
        } else {
            int a=2;
            int b=a;
        }

        av_packet_unref(packet);
    }

    sws_freeContext(pSwsCtx);
    avformat_close_input(&pFormatCtx );
    av_free(pFrame);
    av_free(pFrameRGB);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
    av_frame_free(&filt_frame);
	
    return 0;
}

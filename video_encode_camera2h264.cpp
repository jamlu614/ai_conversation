#include <unistd.h>
#include <thread>
#include <video_encode_camera2h264.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <queue>
#include <algorithm>
#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>

#include <SDL2/SDL.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <SDL2/SDL_audio.h>
}

using namespace std;

// queue<shared_ptr<uint8_t *>> audio_queue;

int flush_encoder(AVFormatContext *fmtCtx, AVCodecContext *codecCtx, int vStreamIndex)
{
    int ret = 0;
    AVPacket *enc_pkt = av_packet_alloc();
    enc_pkt->data = NULL;
    enc_pkt->size = 0;

    if (!(codecCtx->codec->capabilities & AV_CODEC_CAP_DELAY))
        return 0;

    printf("Flushing stream #%u encoder\n", vStreamIndex);
    if (avcodec_send_frame(codecCtx, 0) >= 0)
    {
        while (avcodec_receive_packet(codecCtx, enc_pkt) >= 0)
        {
            printf("success encoder 1 frame.\n");

            // parpare packet for muxing
            enc_pkt->stream_index = vStreamIndex;
            av_packet_rescale_ts(enc_pkt, codecCtx->time_base,
                                 fmtCtx->streams[vStreamIndex]->time_base);
            ret = av_interleaved_write_frame(fmtCtx, enc_pkt);
            if (ret < 0)
            {
                break;
            }
        }
    }

    av_packet_unref(enc_pkt);

    return ret;
}

int captureAudioTask(Configuration &config, AudioCallback audioCallback, bool &exitFlag, bool &enableFlag)
{
    try
    {

        avdevice_register_all();
        // 打开麦克风
        const AVInputFormat *audioFmt = av_find_input_format("alsa");
        AVDictionary *options = NULL;
        av_dict_set(&options, "sample_rate", "16000", 0);
        av_dict_set(&options, "channels", "1", 0);
        // av_dict_set(&options, "stimeout", "3000000", 0);
        AVFormatContext *audiofmtCtx = NULL;
        printf("audio_dev:%s\n", config.audio_dev);
        int ret = avformat_open_input(&audiofmtCtx, config.audio_dev, audioFmt, &options);
        while (ret < 0 && !exitFlag && enableFlag)
        {
            // printf("Cannot open audio input device.\n");
            // return -1;
            usleep(200 * 1000);
            ret = avformat_open_input(&audiofmtCtx, config.audio_dev, audioFmt, &options);
        }
        printf("打开麦克风\n");
        AVStream *audio_stream = audiofmtCtx->streams[0];
        int16_t sample_rate = audio_stream->codecpar->sample_rate;
        printf("Actual sample rate: %d\n", sample_rate);
        if (avformat_find_stream_info(audiofmtCtx, nullptr) < 0)
        {
            printf("Could not find stream information.\n");
            return -1;
        }

        int audioStreamIndex = -1;
        for (unsigned int i = 0; i < audiofmtCtx->nb_streams; i++)
        {
            if (audiofmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                audioStreamIndex = i;
                break;
            }
        }

        if (audioStreamIndex == -1)
        {
            printf("Could not find audio stream.\n");
            return -1;
        }

        AVCodecParameters *codecParameters = audiofmtCtx->streams[audioStreamIndex]->codecpar;
        const AVCodec *decoder = avcodec_find_decoder(codecParameters->codec_id);
        if (!decoder)
        {
            printf("Decoder not found.\n");
            return -1;
        }

        AVCodecContext *codecContext = avcodec_alloc_context3(decoder);
        if (!codecContext)
        {
            printf("Could not allocate audio codec context.\n");
            return -1;
        }

        if (avcodec_parameters_to_context(codecContext, codecParameters) < 0)
        {
            printf("Could not copy codec parameters to codec context.");
            return -1;
        }

        if (avcodec_open2(codecContext, decoder, nullptr) < 0)
        {
            printf("Could not open codec.");
            return -1;
        }

        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        if (!packet || !frame)
        {
            printf("Could not allocate packet or frame.");
            return -1;
        }

        // 打开输出文件
        std::ofstream outputFile("mic.pcm", std::ios::binary);
        if (!outputFile.is_open())
        {
            printf("Could not open output file.");
            return -1;
        }

        // 读取音频数据
        while (av_read_frame(audiofmtCtx, packet) >= 0 && !exitFlag && enableFlag)
        {
            //  printf("循环读取音频\n");
            if (packet->stream_index == audioStreamIndex)
            {
                if (avcodec_send_packet(codecContext, packet) >= 0)
                {
                    while (avcodec_receive_frame(codecContext, frame) >= 0 && !exitFlag && enableFlag)
                    {
                        // printf("循环解析音频\n");
                        // 写入 PCM 数据到文件
                        outputFile.write(reinterpret_cast<const char *>(frame->data[0]), frame->linesize[0]);
                        if (audioCallback && enableFlag && !exitFlag)
                        {
                            // auto now = std::chrono::system_clock::now();
                            // auto milliseconds_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                            // std::cout << "Current timestamp (milliseconds): " << milliseconds_since_epoch << std::endl;

                            uint8_t *data = (uint8_t *)malloc(frame->linesize[0]);
                            memcpy(data, frame->data[0], frame->linesize[0]);
                            // free(data);
                            audioCallback((u_int8_t *)data, frame->linesize[0]);
                            // printf("回调数据\n");
                        }
                    }
                }
            }
            av_packet_unref(packet);
        }

        // 清理资源
        outputFile.close();
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecContext);
        avformat_close_input(&audiofmtCtx);
        printf("销毁麦克风\n");
    }
    catch (const std::exception &e)
    {
        std::cerr << "线程异常: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "线程未知异常" << std::endl;
    }

    return 0;
}

int captrueVideoTask(VideoCallback videoCallback, bool &exitFlag)
{
    try
    {
        int ret = 0;
        avdevice_register_all();

        AVFormatContext *inVideoFmtCtx = avformat_alloc_context();
        AVCodecContext *inVideoCodecCtx = NULL;
        const AVCodec *inVideoCodec = NULL;
        AVPacket *inPkt = av_packet_alloc();
        AVFrame *srcFrame = av_frame_alloc();
        AVFrame *yuvFrame = av_frame_alloc();

        // 打开输出文件，并填充fmtCtx数据
        AVFormatContext *outFmtCtx = avformat_alloc_context();
        const AVOutputFormat *outFmt = NULL;
        AVCodecContext *outCodecCtx = NULL;
        const AVCodec *outCodec = NULL;
        AVStream *outVStream = NULL;

        AVPacket *outPkt = av_packet_alloc();

        struct SwsContext *img_ctx = NULL;

        int inVideoStreamIndex = -1;

        do
        {
            /////////////解码器部分//////////////////////
            // 打开摄像头
            const char *fmt_name = "v4l2";
            const AVInputFormat *inVideoFmt = av_find_input_format(fmt_name);
            AVDictionary *options = NULL;
            av_dict_set(&options, "input_format", "mjpeg", 0); // 指定输入格式为 MJPEG
            // av_dict_set(&options, "input_format", "yuyv422", 0); // 指定输入格式为 yuyv422
            av_dict_set(&options, "video_size", "1280x720", 0);
            av_dict_set(&options, "framerate", "30", 0);
            if (avformat_open_input(&inVideoFmtCtx, "/dev/video1", inVideoFmt, &options) < 0)
            {
                printf("Cannot open camera.\n");
                av_dict_free(&options);
                break;
            }
            av_dict_free(&options);

            if (avformat_find_stream_info(inVideoFmtCtx, NULL) < 0)
            {
                printf("Cannot find any stream in file.\n");
                break;
            }

            for (uint32_t i = 0; i < inVideoFmtCtx->nb_streams; i++)
            {
                if (inVideoFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    inVideoStreamIndex = i;
                    break;
                }
            }
            if (inVideoStreamIndex == -1)
            {
                printf("Cannot find video stream in file.\n");
                break;
            }

            AVCodecParameters *inVideoCodecPara = inVideoFmtCtx->streams[inVideoStreamIndex]->codecpar;
            if (!(inVideoCodec = avcodec_find_decoder(inVideoCodecPara->codec_id)))
            // if (!(inVideoCodec = avcodec_find_decoder_by_name("h264_rkmpp")))
            {
                printf("Cannot find valid video decoder.\n");
                break;
            }
            inVideoCodecPara->codec_id = inVideoCodec->id;
            if (!(inVideoCodecCtx = avcodec_alloc_context3(inVideoCodec)))
            {
                printf("Cannot alloc valid decode codec context.\n");
                break;
            }
            if (avcodec_parameters_to_context(inVideoCodecCtx, inVideoCodecPara) < 0)
            {
                printf("Cannot initialize parameters.\n");
                break;
            }
            if (avcodec_open2(inVideoCodecCtx, inVideoCodec, NULL) < 0)
            {
                printf("Cannot open codec.\n");
                break;
            }

            img_ctx = sws_getContext(inVideoCodecCtx->width,
                                     inVideoCodecCtx->height,
                                     inVideoCodecCtx->pix_fmt,
                                     inVideoCodecCtx->width,
                                     inVideoCodecCtx->height,
                                     AV_PIX_FMT_YUV420P,
                                     SWS_BICUBIC,
                                     NULL, NULL, NULL);

            int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                                                    inVideoCodecCtx->width,
                                                    inVideoCodecCtx->height, 1);
            uint8_t *out_buffer = (unsigned char *)av_malloc(numBytes * sizeof(unsigned char));

            ret = av_image_fill_arrays(yuvFrame->data,
                                       yuvFrame->linesize,
                                       out_buffer,
                                       AV_PIX_FMT_YUV420P,
                                       inVideoCodecCtx->width,
                                       inVideoCodecCtx->height,
                                       1);
            if (ret < 0)
            {
                printf("Fill arrays failed.\n");
                break;
            }
            //////////////解码器部分结束/////////////////////

            //////////////编码器部分开始/////////////////////
            // const char* outFile = "camera.h264";

            // if(avformat_alloc_output_context2(&outFmtCtx,NULL,NULL,outFile)<0){
            //     printf("Cannot alloc output file context.\n");
            //     break;
            // }
            // outFmt = outFmtCtx->oformat;

            // //打开输出文件
            //  if(avio_open(&outFmtCtx->pb,outFile,AVIO_FLAG_READ_WRITE)<0){
            //      printf("output file open failed.\n");avformat_wravformat_write_headerite_header
            //      break;
            //  }

            // // 创建h264视频流，并设置参数
            //  outVStream = avformat_new_stream(outFmtCtx,outCodec);
            //  if(outVStream==NULL){
            //      printf("create new video stream fialed.\n");avformat_write_header
            //      break;
            //  }
            //  outVStream->time_base.den=30;
            //  outVStream->time_base.num=1;

            // // 编码参数相关
            //  AVCodecParameters *outCodecPara = outFmtCtx->streams[outVStream->index]->codecpar;
            //  outCodecPara->codec_type=AVMEDIA_TYPE_VIDEO;
            // //  outCodecPara->codec_id = outFmt->video_codec;
            //  outCodecPara->width = 1280;
            //  outCodecPara->height = 480;
            //  outCodecPara->bit_rate = 400*1000;

            // 查找编码器
            // outCodec = avcodec_find_encoder(outFmt->video_codec);

            outCodec = avcodec_find_encoder_by_name("h264_rkmpp");
            if (outCodec == NULL)
            {
                printf("Cannot find any encoder.\n");
                break;
            }

            // 设置编码器内容
            outCodecCtx = avcodec_alloc_context3(outCodec);
            // avcodec_parameters_to_context(outCodecCtx,outCodecPara);
            if (outCodecCtx == NULL)
            {
                printf("Cannot alloc output codec content.\n");
                break;
            }

            outCodecCtx->codec_id = outCodec->id;
            outCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
            outCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
            outCodecCtx->width = inVideoCodecCtx->width;
            outCodecCtx->height = inVideoCodecCtx->height;
            outCodecCtx->time_base.num = 1;
            outCodecCtx->time_base.den = 30;
            outCodecCtx->bit_rate = 1000 * 1000;
            outCodecCtx->gop_size = 30;

            if (outCodecCtx->codec_id == AV_CODEC_ID_H264)
            {
                outCodecCtx->qmin = 10;
                outCodecCtx->qmax = 51;
                outCodecCtx->qcompress = (float)0.6;
            }
            else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO)
            {
                outCodecCtx->max_b_frames = 0;
            }
            else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO)
            {
                outCodecCtx->mb_decision = 2;
            }

            // 打开编码器
            if (avcodec_open2(outCodecCtx, outCodec, NULL) < 0)
            {
                printf("Open encoder failed.\n");
                break;
            }
            ///////////////编码器部分结束////////////////////

            ///////////////编解码部分//////////////////////
            yuvFrame->format = outCodecCtx->pix_fmt;
            yuvFrame->width = outCodecCtx->width;
            yuvFrame->height = outCodecCtx->height;

            // ret = avformat_write_header(outFmtCtx,NULL);

            // int count = 0;
            // while (av_read_frame(inFmtCtx, iinPktnPkt) >= 0 && count < 50)

            while (av_read_frame(inVideoFmtCtx, inPkt) >= 0 && !exitFlag)
            {
                auto start = std::chrono::high_resolution_clock::now();
                if (inPkt->stream_index == inVideoStreamIndex)
                {
                    if (avcodec_send_packet(inVideoCodecCtx, inPkt) >= 0)
                    {
                        while ((ret = avcodec_receive_frame(inVideoCodecCtx, srcFrame)) >= 0 && !exitFlag)
                        {
                            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                                return -1;
                            else if (ret < 0)
                            {
                                fprintf(stderr, "Error during decoding\n");
                                exit(1);
                            }
                            sws_scale(img_ctx,
                                      (const uint8_t *const *)srcFrame->data,
                                      srcFrame->linesize,
                                      0, inVideoCodecCtx->height,
                                      yuvFrame->data, yuvFrame->linesize);

                            yuvFrame->pts = srcFrame->pts;
                            // encode
                            if (avcodec_send_frame(outCodecCtx, yuvFrame) >= 0)
                            {
                                if (avcodec_receive_packet(outCodecCtx, outPkt) >= 0)
                                {
                                    // printf("encode %d frame.\n", count);
                                    // ++count;
                                    // outPkt->stream_index = outVStream->index;
                                    // av_packet_rescale_ts(outPkt,outCodecCtx->time_base,
                                    //                      outVStream->time_base);
                                    // outPkt->pos = -1;
                                    // av_interleaved_write_frame(outFmtCtx,outPkt);
                                    // auto end = std::chrono::high_resolution_clock::now();
                                    // // 计算时间差，转换为毫秒
                                    // auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                                    // // 输出时间差，以毫秒为单位
                                    // std::cout << "Elapsed time: " << duration_ms.count() << " milliseconds" << std::endl;
                                    if (videoCallback)
                                    {
                                        uint8_t *data = (uint8_t *)av_malloc(outPkt->size);
                                        memcpy(data, outPkt->data, outPkt->size);
                                        videoCallback(data, outPkt->size, outPkt->flags & AV_PKT_FLAG_KEY);
                                    }

                                    av_packet_unref(outPkt);
                                }
                            }
                            // usleep(1000 * 24);
                        }
                    }
                    av_packet_unref(inPkt);
                    // fflush(stdout);
                }

                // auto start = std::chrono::high_resolution_clock::now();
                // int numBytes = av_image_get_buffer_size(inVideoCodecCtx->pix_fmt,
                //                                         inVideoCodecCtx->width,
                //                                         inVideoCodecCtx->height, 1);
                // uint8_t *src_buffer = (unsigned char *)av_malloc(numBytes * sizeof(unsigned char));

                // ret = av_image_fill_arrays(srcFrame->data,
                //                            srcFrame->linesize,
                //                            src_buffer,
                //                            inVideoCodecCtx->pix_fmt,
                //                            inVideoCodecCtx->width,
                //                            inVideoCodecCtx->height,
                //                            1);
                // memcpy(src_buffer, inPkt->data, inPkt->size);
                // sws_scale(img_ctx,
                //           (const uint8_t *const *)srcFrame->data,
                //           srcFrame->linesize,
                //           0, inVideoCodecCtx->height,
                //           yuvFrame->data, yuvFrame->linesize);

                // yuvFrame->pts = srcFrame->pts;

                // if (avcodec_send_frame(outCodecCtx, yuvFrame) >= 0)
                // {
                //     if (avcodec_receive_packet(outCodecCtx, outPkt) >= 0)
                //     {
                //         // printf("encode %d frame.\n", count);
                //         // ++count;
                //         // outPkt->stream_index = outVStream->index;
                //         // av_packet_rescale_ts(outPkt,outCodecCtx->time_base,
                //         //                      outVStream->time_base);
                //         // outPkt->pos = -1;
                //         // av_interleaved_write_frame(outFmtCtx,outPkt);
                //         auto end = std::chrono::high_resolution_clock::now();
                //         // 计算时间差，转换为毫秒
                //         auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                //         // 输出时间差，以毫秒为单位
                //         std::cout << "Elapsed time: " << duration_ms.count() << " milliseconds" << std::endl;
                //         if (videoCallback)
                //         {
                //             uint8_t *data = (uint8_t *)av_malloc(outPkt->size);
                //             memcpy(data, outPkt->data, outPkt->size);
                //             videoCallback(data, outPkt->size, outPkt->flags & AV_PKT_FLAG_KEY);
                //         }

                //         av_packet_unref(outPkt);
                //     }
                // }
            }

            // ret = flush_encoder(outFmtCtx, outCodecCtx, outVStream->index);
            // if (ret < 0)
            // {
            //     printf("flushing encoder failed.\n");
            //     break;
            // }

            // av_write${CMAKE_SOURCE_DIR}/common_trailer(outFmtCtx);
            ////////////////编解码部分结束////////////////
        } while (0);

        ///////////内存释放部分/////////////////////////
        av_packet_free(&inPkt);
        avcodec_free_context(&inVideoCodecCtx);
        avcodec_close(inVideoCodecCtx);
        avformat_close_input(&inVideoFmtCtx);
        av_frame_free(&srcFrame);
        av_frame_free(&yuvFrame);

        av_packet_free(&outPkt);
        avcodec_free_context(&outCodecCtx);
        avcodec_close(outCodecCtx);
        avformat_close_input(&outFmtCtx);
    }
    catch (const std::exception &e)
    {
        std::cerr << "线程异常: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "线程未知异常" << std::endl;
    }

    return 0;
}

void startCaptrueAudio(Configuration &config, AudioCallback audioCallback,
                       bool &exitFlag, bool &enableFlag)
{
    captureAudioTask(config, audioCallback, exitFlag, enableFlag);
}

SDL_AudioDeviceID dev;
void startCaptrueVideo(VideoCallback videoCallback,
                       bool &exitFlag)
{
    captrueVideoTask(videoCallback, exitFlag);
}

// queue<shared_ptr<uint8_t[]>> audio_queue; // 缓冲区
void audio_callback(void *userdata, uint8_t *stream, int len);
int audio_device_open = 0;
void startPlayAudio(Configuration &config, int channels, int sample_rate,
                    bool &exitFlag, bool &onLineTTSFlag)
{
    try
    {
        // 使用 SDL 播放 PCM 数据
        while (SDL_Init(SDL_INIT_AUDIO) < 0 && !exitFlag && onLineTTSFlag)
        {
            fprintf(stderr, "Could not initialize SDL: ");
            fprintf(stderr, "SDL Error: %s\n", SDL_GetError());
            usleep(500 * 1000);
            continue;
        }
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = sample_rate;
        want.format = AUDIO_S16SYS;
        want.channels = channels;
        want.silence = 0;
        want.samples = 4096;
        want.callback = NULL; // 回调函数，用于填充音频数据
        want.userdata = NULL;

        // 获取音频设备的数量
        int num_devices = SDL_GetNumAudioDevices(0);
        if (num_devices <= 0)
        {
            fprintf(stderr, "No audio devices available\n");
            SDL_Quit();
            return;
        }

        // const char *device_name =NULL;
        std::cout << "device name:" << config.play_audio_dev << std::endl;
        const char *device_name = config.play_audio_dev;
        //  const char *device_name = "rk3xxx, USB Audio";
        // std::string sub_str("ES8323");
        // printf("Audio device num:%d\n", num_devices);
        // // 打印可用的音频设备
        for (int i = 0; i < num_devices; ++i)
        {
            std::string str = std::string(SDL_GetAudioDeviceName(i, 0));
            printf("Audio device %d: %s\n", i, str.c_str());
            // device_name = str.c_str();
        }
        // device_name = SDL_GetAudioDeviceName(num_devices-1, 0);

        dev = SDL_OpenAudioDevice(device_name, 0, &want, &have, 0);
        // dev = SDL_OpenAudio(&want,&have);
        while (dev == 0 && !exitFlag && onLineTTSFlag)
        {
            printf("无法打开音频设备! SDL_Error: %s\n", SDL_GetError());
            usleep(500 * 1000);
            printf("重试打开\n");
            dev = SDL_OpenAudioDevice(device_name, 0, &want, &have, 0);
            // SDL_Quit();
            // return;
        }
        audio_device_open = 1;
        // SDL_QueueAudio(dev, pcm_data, pcm_size);
        // SDL_PauseAudio(0); // 开始播放
        printf("开始播放音频\n");
        SDL_PauseAudioDevice(dev, 0);
        // 等待播放完成
        while (!exitFlag && onLineTTSFlag)
        {
            SDL_Delay(100);
        }

        SDL_CloseAudio();
        SDL_Quit();
        audio_device_open = 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "线程异常: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "线程未知异常" << std::endl;
    }
}

void pushPCM(uint8_t *pcm_data, int pcm_size)
{
    // printf("push音频数据到队列\n");
    if (audio_device_open)
    {
        // shared_ptr<uint8_t *> data = make_shared<uint8_t *>(pcm_data);
        // audio_queue.push(data);
        SDL_QueueAudio(dev, pcm_data, pcm_size);
    }
}

void audio_callback(void *userdata, uint8_t *stream, int len)
{

    // static std::vector<uint8_t> buffer;
    // static size_t bufferIndex = 0;
    // static std::mutex mtx;
    // static std::condition_variable cv;
    // static bool done = false;

    // std::unique_lock<std::mutex> lock(mtx);

    // // 如果缓冲区为空，则填充它
    // while (bufferIndex >= buffer.size() && !done)
    // {
    //     if (!audio_queue.empty())
    //     {
    //         uint8_t *net_buffer = audio_queue.front().get();
    //         size_t dataLength = sizeof(net_buffer) / sizeof(net_buffer[0]);
    //         std::vector<uint8_t> vectorData(net_buffer, net_buffer + dataLength);
    //         buffer = vectorData;
    //         bufferIndex = 0;
    //         if (buffer.empty())
    //         {
    //             done = true; // 没有更多数据，结束播放
    //         }
    //         audio_queue.pop();
    //         cv.notify_one();
    //     }
    //     else
    //     {
    //         SDL_Delay(100);
    //     }
    // }

    // // 从缓冲区复制数据到输出流
    // size_t toCopy = std::min(static_cast<size_t>(len), buffer.size() - bufferIndex);
    // memcpy(stream, buffer.data() + bufferIndex, toCopy);
    // bufferIndex += toCopy;

    // // 如果没有更多数据且缓冲区为空，则填充静音（可选）
    // if (toCopy < len)
    // {
    //     memset(stream + toCopy, 0, len - toCopy);
    // }
}

// int main()
// {
//     int ret = 0;
//     avdevice_register_all();

//     AVFormatContext *inFmtCtx = avformat_alloc_context();
//     AVCodecContext *inCodecCtx = NULL;
//     const AVCodec *inCodec = NULL;
//     AVPacket *inPkt = av_packet_alloc();
//     AVFrame *srcFrame = av_frame_alloc();
//     AVFrame *yuvFrame = av_frame_alloc();

//     // 打开输出文件，并填充fmtCtx数据
//     AVFormatContext *outFmtCtx = avformat_alloc_context();
//     const AVOutputFormat *outFmt = NULL;
//     AVCodecContext *outCodecCtx = NULL;
//     const AVCodec *outCodec = NULL;
//     AVStream *outVStream = NULL;

//     AVPacket *outPkt = av_packet_alloc();

//     struct SwsContext *img_ctx = NULL;

//     int inVideoStreamIndex = -1;

//     do
//     {
//         /////////////解码器部分//////////////////////
//         // 打开摄像头
//         const char *fmt_name = "v4l2";
//         const AVInputFormat *inVideoFmt = av_find_input_format(fmt_name);
//         if (avformat_open_input(&inFmtCtx, "/dev/video0", inVideoFmt, NULL) < 0)
//         {
//             printf("Cannot open camera.\n");
//             break;
//         }

//         if (avformat_find_stream_info(inFmtCtx, NULL) < 0)
//         {
//             printf("Cannot find any stream in file.\n");
//             break;
//         }

//         for (uint32_t i = 0; i < inFmtCtx->nb_streams; i++)
//         {
//             if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
//             {
//                 inVideoStreamIndex = i;
//                 break;
//             }
//         }
//         if (inVideoStreamIndex == -1)
//         {
//             printf("Cannot find video stream in file.\n");
//             break;
//         }

//         AVCodecParameters *inVideoCodecPara = inFmtCtx->streams[inVideoStreamIndex]->codecpar;
//         if (!(inCodec = avcodec_find_decoder(inVideoCodecPara->codec_id)))
//         {
//             printf("Cannot find valid video decoder.\n");
//             break;
//         }
//         if (!(inCodecCtx = avcodec_alloc_context3(inCodec)))
//         {
//             printf("Cannot alloc valid decode codec context.\n");
//             break;
//         }
//         if (avcodec_parameters_to_context(inCodecCtx, inVideoCodecPara) < 0)
//         {
//             printf("Cannot initialize parameters.\n");
//             break;
//         }

//         if (avcodec_open2(inCodecCtx, inCodec, NULL) < 0)
//         {
//             printf("Cannot open codec.\n");
//             break;
//         }

//         img_ctx = sws_getContext(inCodecCtx->width,
//                                  inCodecCtx->height,
//                                  inCodecCtx->pix_fmt,
//                                  inCodecCtx->width,
//                                  inCodecCtx->height,
//                                  AV_PIX_FMT_YUV420P,
//                                  SWS_BICUBIC,
//                                  NULL, NULL, NULL);

//         int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
//                                                 inCodecCtx->width,
//                                                 inCodecCtx->height, 1);
//         uint8_t *out_buffer = (unsigned char *)av_malloc(numBytes * sizeof(unsigned char));

//         ret = av_image_fill_arrays(yuvFrame->data,
//                                    yuvFrame->linesize,
//                                    out_buffer,
//                                    AV_PIX_FMT_YUV420P,
//                                    inCodecCtx->width,
//                                    inCodecCtx->height,
//                                    1);
//         if (ret < 0)
//         {
//             printf("Fill arrays failed.\n");
//             break;
//         }
//         //////////////解码器部分结束/////////////////////

//         //////////////编码器部分开始/////////////////////
//         // const char* outFile = "camera.h264";

//         // if(avformat_alloc_output_context2(&outFmtCtx,NULL,NULL,outFile)<0){
//         //     printf("Cannot alloc output file context.\n");
//         //     break;
//         // }
//         // outFmt = outFmtCtx->oformat;

//         // //打开输出文件
//         //  if(avio_open(&outFmtCtx->pb,outFile,AVIO_FLAG_READ_WRITE)<0){
//         //      printf("output file open failed.\n");avformat_wravformat_write_headerite_header
//         //      break;
//         //  }

//         // // 创建h264视频流，并设置参数
//         //  outVStream = avformat_new_stream(outFmtCtx,outCodec);
//         //  if(outVStream==NULL){
//         //      printf("create new video stream fialed.\n");avformat_write_header
//         //      break;
//         //  }
//         //  outVStream->time_base.den=30;
//         //  outVStream->time_base.num=1;

//         // // 编码参数相关
//         //  AVCodecParameters *outCodecPara = outFmtCtx->streams[outVStream->index]->codecpar;
//         //  outCodecPara->codec_type=AVMEDIA_TYPE_VIDEO;
//         //  outCodecPara->codec_id = outFmt->video_codec;
//         //  outCodecPara->width = 1280;
//         //  outCodecPara->height = 480;
//         //  outCodecPara->bit_rate = 400*1000;

//         // 查找编码器
//         outCodec = avcodec_find_encoder(outFmt->video_codec);
//         if (outCodec == NULL)
//         {
//             printf("Cannot find any encoder.\n");
//             break;
//         }

//         // 设置编码器内容
//         outCodecCtx = avcodec_alloc_context3(outCodec);
//         // avcodec_parameters_to_context(outCodecCtx,outCodecPara);
//         if (outCodecCtx == NULL)
//         {
//             printf("Cannot alloc output codec content.\n");
//             break;
//         }

//         outCodecCtx->codec_id = outFmt->video_codec;
//         outCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
//         outCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
//         outCodecCtx->width = inCodecCtx->width;
//         outCodecCtx->height = inCodecCtx->height;
//         outCodecCtx->time_base.num = 1;
//         outCodecCtx->time_base.den = 30;
//         outCodecCtx->bit_rate = 400 * 1000;
//         outCodecCtx->gop_size = 10;

//         if (outCodecCtx->codec_id == AV_CODEC_ID_H264)
//         {
//             outCodecCtx->qmin = 10;
//             outCodecCtx->qmax = 51;
//             outCodecCtx->qcompress = (float)0.6;
//         }
//         else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO)
//         {
//             outCodecCtx->max_b_frames = 2;
//         }
//         else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO)
//         {
//             outCodecCtx->mb_decision = 2;
//         }

//         // 打开编码器
//         if (avcodec_open2(outCodecCtx, outCodec, NULL) < 0)
//         {
//             printf("Open encoder failed.\n");
//             break;
//         }
//         ///////////////编码器部分结束////////////////////

//         ///////////////编解码部分//////////////////////
//         yuvFrame->format = outCodecCtx->pix_fmt;
//         yuvFrame->width = outCodecCtx->width;
//         yuvFrame->height = outCodecCtx->height;

//         // ret = avformat_write_header(outFmtCtx,NULL);

//         // int count = 0;
//         // while (av_read_frame(inFmtCtx, iinPktnPkt) >= 0 && count < 50)
//         while (av_read_frame(inFmtCtx, inPkt) >= 0)
//         {
//             if (inPkt->stream_index == inVideoStreamIndex)
//             {
//                 if (avcodec_send_packet(inCodecCtx, inPkt) >= 0)
//                 {
//                     while ((ret = avcodec_receive_frame(inCodecCtx, srcFrame)) >= 0)
//                     {
//                         if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
//                             return -1;
//                         else if (ret < 0)
//                         {
//                             fprintf(stderr, "Error during decoding\n");
//                             exit(1);
//                         }
//                         sws_scale(img_ctx,
//                                   (const uint8_t *const *)srcFrame->data,
//                                   srcFrame->linesize,
//                                   0, inCodecCtx->height,
//                                   yuvFrame->data, yuvFrame->linesize);

//                         yuvFrame->pts = srcFrame->pts;
//                         // encode
//                         if (avcodec_send_frame(outCodecCtx, yuvFrame) >= 0)
//                         {
//                             if (avcodec_receive_packet(outCodecCtx, outPkt) >= 0)
//                             {
//                                 // printf("encode %d frame.\n", count);
//                                 // ++count;
//                                 // outPkt->stream_index = outVStream->index;
//                                 // av_packet_rescale_ts(outPkt,outCodecCtx->time_base,
//                                 //                      outVStream->time_base);
//                                 // outPkt->pos = -1;
//                                 // av_interleaved_write_frame(outFmtCtx,outPkt);

//                                 av_packet_unref(outPkt);
//                             }
//                         }
//                         // usleep(1000 * 24);
//                     }
//                 }
//                 av_packet_unref(inPkt);
//                 // fflush(stdout);
//             }
//         }

//         // ret = flush_encoder(outFmtCtx, outCodecCtx, outVStream->index);
//         // if (ret < 0)
//         // {
//         //     printf("flushing encoder failed.\n");
//         //     break;
//         // }

//         // av_write${CMAKE_SOURCE_DIR}/common_trailer(outFmtCtx);
//         ////////////////编解码部分结束////////////////
//     } while (0);

//     ///////////内存释放部分/////////////////////////
//     av_packet_free(&inPkt);
//     avcodec_free_context(&inCodecCtx);
//     avcodec_close(inCodecCtx);
//     avformat_close_input(&inFmtCtx);
//     av_frame_free(&srcFrame);
//     av_frame_free(&yuvFrame);

//     av_packet_free(&outPkt);
//     avcodec_free_context(&outCodecCtx);
//     avcodec_close(outCodecCtx);
//     avformat_close_input(&outFmtCtx);

//     return 0;
// }
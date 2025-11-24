//  Agora RTC/MEDIA SDK
//
//  Created by Jay Zhang in 2020-04.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//

// video
//    ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//   {SDK::capture video}  ==>  |raw data|  ==>  {SDK::encode B}  ==>  |encoded data| ==> {SDK::send}  ==> {AGORA::VOS}  ==>  {SDK::receive} ==> |encoded data|  ==> {SDK::decode} ==> |raw data|
//    ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//                                                                                   sample send h264(this sample)                              sample receive h264

// This sample will show how to use the SDK to send the encoded_Video to the Agora_channel
// As a user,you should papera the encoded_Video and create the Agora_service,Agora_connection, Agora_EncodedImage_sender and Agora_local_video_track
// You should parse the encoded_Video and send to sdk one frame by one frame.(by use the AGORA_API: Sender->sendEncodedVideoImage())
// The class HelperH264FileParser is a common_helper class to parse the h264 video
// And all Agora necessary Classes builded in main() function
// Last, the sendVideoThread call Sender->sendEncodedVideoImage() to send the encoded videoFrame

// The sdk also provide lots of call_back functions to help user get the network states , local states and  peer states
// The callback functions can be found in **observer , you should register the observer first.

// the SDKapi call flows:
// service = createAndInitAgoraService()
//     connection = service->createRtcConnection()
//         connection->registerObserver()
//             connection->connect()
//         factory = service->createMediaNodeFactory()
//             VideoSender = factory->createVideoEncodedImageSender();
//                 VideoTrack = service->createCustomVideoTrack()
//      connection->getLocalUser()->publishAudio(VideoTrack);
//                 Sender->sendEncodedVideoImage()

// audio
//    ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//   {SDK::capture audio}  ==>  |raw data|  ==>  {SDK::encode B}  ==>  |encoded data| ==> {SDK::send}  ==> {AGORA::VOS}  ==>  {SDK::receive} ==> |encoded data|  ==> {SDK::decode} ==> |raw data|
//    ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//                         sample send pcm(this sample)                sample send opus                                                                                            sample receive pcm

// This sample will show how to use the SDK to send the raw AudioData(pcm) to the Agora_channel
// As a user,you should papera the audio and create the Agora_service,Agora_connection, Agora_audio_pcm_sender and Agora_local_audio_track
// You should parse the audio and send to sdk one frame by one frame.(by use the AGORA_API: Sender->sendAudioPcmData())
// And all Agora necessary Classes builded in main() function
// Last, the sendAudioThread call Sender->sendAudioPcmData() to send the audioFrame

// The sdk also provide lots of call_back functions to help user get the network states , local states and  peer states
// The callback functions can be found in **observer , you should register the observer first.

// the SDKapi call flows:
// service = createAndInitAgoraService()
//     connection = service->createRtcConnection()
//         connection->registerObserver()
//         connection->connect()
//         factory = service->createMediaNodeFactory()
//             AudioSender = factory->createAudioPcmDataSender();
//                 AudioTrack = service->createCustomAudioTrack();
//      connection->getLocalUser()->publishAudio(audioTrack);
//                 Sender->sendAudioPcmData()

// The destruct order of all the class can be find in the main function end.

// Wish you have a great experience with Agora_SDK!

#include <csignal>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/helper.h"
#include "common/log.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"

#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"

#include "NGIAgoraMediaNode.h"
#include "NGIAgoraVideoTrack.h"

#include "helper_h264_parser.h"

#include "video_encode_camera2h264.hpp"
#include <queue>
#include <curl/curl.h>
#include <mutex>
#include <iostream>
#include <fstream>
#include "RtcTokenBuilder2.h"

#include <nlohmann/json.hpp>
#include <mosquitto.h>
#include <ini.h>
#include <libgen.h> // for dirname

#include "sample_send_h264_pcm.h"
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#include <algorithm>

// 模拟音频缓冲区
short mic_in[FRAME_SIZE] = {0};    // 麦克风输入（含回声）
short ref_in[FRAME_SIZE] = {0};    // 远端参考信号
short clean_out[FRAME_SIZE] = {0}; // 处理后输出
SpeexEchoState *echo_state;
SpeexPreprocessState *preprocess;
bool startPlay = false;
bool startCancellation = false;
int64_t time_stamp;

using namespace std;
using json = nlohmann::json;

CURL *curl;
CURLcode res;
std::string readBuffer;
uint8_t *vad_buff = NULL;
static std::queue<std::shared_ptr<HelperH264Frame>> micAudioQueue;
static std::queue<std::shared_ptr<uint8_t *>> speakAudioQueue;
static std::queue<std::shared_ptr<uint8_t *>> sendAudioQueue;

static bool exitFlag = false;
static bool captureAudioFlag = false;
static bool playAudioFlag = false;
static bool sendAudioFlag = false;
static bool initAiFlag = false;
std::thread playAudioThread;
std::thread captureAudioThread;
std::thread sendAudioThread;
std::thread destoryThread;
std::thread vadThread;
agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender;
// const char *app_id = "4dfbb27cc2a64ac9b43135be289a858e";
// const char *app_certificate = "0abbf21056df4efabd4b1a66528f75c1";
std::string agent_id;
Configuration config;
struct mosquitto *mosq;

std::ofstream micAudioFile("mic2.pcm", std::ios::binary);
std::ofstream playAudioFile("speak.pcm", std::ios::binary);
std::ofstream cleanAudioFile("clean.pcm", std::ios::binary);

void publishMessage(mosquitto *mosq, const char *message, const char *topic);

class PcmFrameObserver : public agora::media::IAudioFrameObserverBase
{
public:
  PcmFrameObserver(const std::string &outputFilePath)
      : outputFilePath_(outputFilePath),
        pcmFile_(nullptr),
        fileCount(0),
        fileSize_(0) {}

  bool onPlaybackAudioFrame(const char *channelId, AudioFrame &audioFrame) override { return true; };

  bool onRecordAudioFrame(const char *channelId, AudioFrame &audioFrame) override { return true; };

  bool onMixedAudioFrame(const char *channelId, AudioFrame &audioFrame) override { return true; };

  bool onEarMonitoringAudioFrame(AudioFrame &audioFrame) { return true; };

  bool onPlaybackAudioFrameBeforeMixing(const char *channelId, agora::media::base::user_id_t userId, AudioFrame &audioFrame) override;

  int getObservedAudioFramePosition() override { return 0; };

  AudioParams getPlaybackAudioParams() override { return AudioParams(); };

  AudioParams getRecordAudioParams() override { return AudioParams(); };

  AudioParams getMixedAudioParams() override { return AudioParams(); };

  AudioParams getEarMonitoringAudioParams() override { return AudioParams(); };

private:
  std::string outputFilePath_;
  FILE *pcmFile_;
  int fileCount;
  int fileSize_;
};

class H264FrameReceiver : public agora::media::IVideoEncodedFrameObserver
{
public:
  H264FrameReceiver(const std::string &outputFilePath)
      : outputFilePath_(outputFilePath),
        h264File_(nullptr),
        fileCount(0),
        fileSize_(0) {}

  bool onEncodedVideoFrameReceived(agora::rtc::uid_t uid, const uint8_t *imageBuffer, size_t length,
                                   const agora::rtc::EncodedVideoFrameInfo &videoEncodedFrameInfo) override;

private:
  std::string outputFilePath_;
  FILE *h264File_;
  int fileCount;
  int fileSize_;
};

static std::queue<std::shared_ptr<HelperH264Frame>> videoQueue;
void videoCallback(u_int8_t *data, int len, bool isKeyFrame)
{
  std::shared_ptr<HelperH264Frame> h264Frame = std::make_shared<HelperH264Frame>();
  auto *h264FrameP = h264Frame.get();
  h264FrameP->buffer = std::unique_ptr<uint8_t[]>(data);
  h264FrameP->bufferLen = len;
  h264FrameP->isKeyFrame = isKeyFrame;
  videoQueue.push(h264Frame);
}

#define SAMPLE_BIT_DEPTH 16
// 定义PCM音频样本的最大和最小值（16位）
#define SAMPLE_MAX ((int16_t)((1 << (SAMPLE_BIT_DEPTH - 1)) - 1))
#define SAMPLE_MIN ((int16_t)(-(1 << (SAMPLE_BIT_DEPTH - 1))))
// 将音频样本乘以增益因子，并限制结果在有效范围内
int16_t apply_gain(int16_t sample, float gain) {
    int32_t scaled_sample = (int32_t)(sample * gain);
    
    // 防止溢出，限制值在SAMPLE_MIN和SAMPLE_MAX之间
    if (scaled_sample > SAMPLE_MAX) {
        scaled_sample = SAMPLE_MAX;
    } else if (scaled_sample < SAMPLE_MIN) {
        scaled_sample = SAMPLE_MIN;
    }
    return (int16_t)scaled_sample;
}

// 调节PCM音频的音量
void adjust_pcm_volume(short* input, short* output, size_t num_samples, float gain) {
    for (size_t i = 0; i < num_samples; i++) {
        // 假设num_samples是立体声样本的总数，因此每次迭代处理两个样本（左和右）
        output[i] = apply_gain(input[i], gain);   // 左声道
    }
}


// static uint8_t playBuf[DEFAULT_NUM_OF_CHANNELS * sizeof(int16_t) * DEFAULT_SAMPLE_RATE / 100];
static uint8_t micBuf[DEFAULT_NUM_OF_CHANNELS * sizeof(int16_t) * DEFAULT_SAMPLE_RATE / 100];
int curSampleSize = 0;
void audioCallback(u_int8_t *data, int len)
{
  // auto now = std::chrono::system_clock::now();
  // auto now_time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  // if (time_stamp != 0 && now_time_stamp - time_stamp >= 216)
  // // if (startPlay)
  // {
  // int is_speech = speex_preprocess_run(preprocess, (short *)data);
  // if (is_speech == 0)
  // {
  //   printf("释放\n");
  //   // free(data);
  //   printf("释放完毕\n");
  //   // return;
  // }
  // if(time_stamp==0)
  // {
  //   free(data);
  //   return;
  // }
  std::shared_ptr<HelperH264Frame> h264Frame = std::make_shared<HelperH264Frame>();
  auto *h264FrameP = h264Frame.get();
  h264FrameP->buffer = std::unique_ptr<uint8_t[]>((uint8_t *)data);
  h264FrameP->bufferLen = len;
  micAudioQueue.push(h264Frame);
  if (captureAudioFlag && micAudioQueue.size() >= 5)
  {
    int sampleSize = sizeof(int16_t) * DEFAULT_NUM_OF_CHANNELS;
    int samplesPer10ms = DEFAULT_SAMPLE_RATE / 100;
    int sendBytes = sampleSize * samplesPer10ms;

    uint8_t *frame = nullptr;
    while (curSampleSize < sendBytes && !exitFlag)
    {
      // std::cout << "curSampleSize:" << curSampleSize << std::endl;
      frame = micAudioQueue.front().get()->buffer.get();
      int buff_len = micAudioQueue.front().get()->bufferLen;
      memcpy(micBuf + curSampleSize, frame, buff_len);
      curSampleSize += buff_len;
      micAudioQueue.pop();
    }
    curSampleSize = 0;
    uint8_t *temp_send_data = (uint8_t *)malloc(sendBytes);
    // adjust_pcm_volume((short*)micBuf,(short*)temp_send_data,sendBytes/2,2.0);
    memcpy(temp_send_data, micBuf, sendBytes);
    // micAudioFile.write((char *)temp_send_data, sendBytes);
    shared_ptr<uint8_t *> send_data = make_shared<uint8_t *>(temp_send_data);
    sendAudioQueue.push(send_data);
  }
}

bool PcmFrameObserver::onPlaybackAudioFrameBeforeMixing(const char *channelId, agora::media::base::user_id_t userId, AudioFrame &audioFrame)
{
  size_t audio_size = audioFrame.samplesPerChannel * audioFrame.channels * sizeof(int16_t);
  uint8_t *data = (uint8_t *)audioFrame.buffer;
  int channel = audioFrame.samplesPerChannel;
  int samples = audioFrame.samplesPerSec;
  // printf("samples:%d\n",samples);
  // printf("channels:%d\n",audioFrame.channels );
  // printf("收到音频数据\n");

  // if (!startPlay)
  // {
  //   int is_speech = speex_preprocess_run(preprocess, (short *)data);

  //   if (is_speech != 0)
  //   {
  //     // 当前帧为活动
  //     if (!startPlay)
  //     {
  //       startPlay = true;
  //       auto now = std::chrono::system_clock::now();
  //       time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  //       std::cout << "start time" << time_stamp << std::endl;
  //     }
  //   }
  // }

  // if (startPlay)
  // {
  // {
  //   lock_guard<std::mutex> lock(play_mtx);
  //   memcpy(playBuf, data, audio_size);
  // }
  if (vad_buff == NULL)
  {
    vad_buff = (uint8_t *)malloc(audio_size);
    memcpy(vad_buff, data, audio_size);
  }

  // auto cp_data = (uint8_t *)malloc(audio_size);
  // memcpy(cp_data, data, audio_size);
  // shared_ptr<uint8_t *> temp_data = make_shared<uint8_t *>(cp_data);
  // speakAudioQueue.push(temp_data);
  playAudioFile.write(reinterpret_cast<const char *>(data), audio_size);
  pushPCM(data, audio_size);
  return true;
  // }
  // else
  // {
  //   return false;
  // }
}

// static uint8_t frameBuf[DEFAULT_NUM_OF_CHANNELS * sizeof(int16_t) * DEFAULT_SAMPLE_RATE / 100];
// int curSampleSize = 0;

static void sendOnePcmFrame(const SampleOptions &options,
                            agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender)
{
  // static FILE *file = nullptr;
  // const char *fileName = options.audioFile.c_str();

  // Calculate byte size for 10ms audio samples
  int sampleSize = sizeof(int16_t) * options.audio.numOfChannels;
  int samplesPer10ms = options.audio.sampleRate / 100;
  int sendBytes = sampleSize * samplesPer10ms;

  // uint8_t *frame = nullptr;
  // int nextOffset = 0;
  // int offset = 0;
  // // std::lock_guard<std::mutex> lock(mtx);
  // while (curSampleSize < sendBytes && !exitFlag)
  // {
  //   if (!micAudioQueue.empty() && enableAudioFlag)
  //   {
  //     int len = 0;
  //     frame = micAudioQueue.front().get()->buffer.get();
  //     len = micAudioQueue.front().get()->bufferLen;
  //     if (curSampleSize + len <= sendBytes)
  //     {
  //       memcpy(frameBuf + curSampleSize, frame, len);
  //       // printf("祯长度:%d\n", len);
  //       // printf("当前长度:%d\n", curSampleSize);
  //       // printf("拷贝长度:%d\n", len);
  //     }
  //     else
  //     {
  //       offset = sendBytes - curSampleSize;
  //       nextOffset = len - offset;
  //       // printf("nextOffset:%d\n", nextOffset);
  //       // printf("offset:%d\n", offset);
  //       memcpy(frameBuf + curSampleSize, frame, offset);
  //       // printf("祯长度:%d\n", len);
  //       // printf("当前长度:%d\n", curSampleSize);
  //       // printf("拷贝长度:%d\n", offset);
  //       // printf("一祯音频数据\n");
  //     }
  //     curSampleSize += len;
  //     micAudioQueue.pop();
  //   }
  //   else
  //   {
  //     usleep(10 * 1000);
  //   }
  // }
  // captureAudioFile.write(reinterpret_cast<const char *>(frameBuf), sendBytes);
  // printf("发送音频数据\n");
  int ret = 0;
  if (!sendAudioQueue.empty() && captureAudioFlag)
  {
    cleanAudioFile.write((char *)*sendAudioQueue.front().get(), sendBytes);
    ret = audioFrameSender->sendAudioPcmData(
        reinterpret_cast<const char *>(*sendAudioQueue.front().get()), 0, samplesPer10ms, agora::rtc::TWO_BYTES_PER_SAMPLE,
        options.audio.numOfChannels, options.audio.sampleRate);
    sendAudioQueue.pop();
    if (ret < 0)
    {
      AG_LOG(ERROR, "Failed to send audio frame!");
      return;
    }
  }

  // memset(frameBuf, 0, sendBytes);
  // if (nextOffset > 0)
  // {
  //   memcpy(frameBuf, frame + offset, nextOffset);
  //   // printf("剩余拷贝长度:%d\n", nextOffset);
  //   curSampleSize = nextOffset;
  // }
  // else
  // {
  //   curSampleSize = 0;
  // }

  //--------------------------------------------------------------------------
  // if (!audioQueue.empty())
  // {
  //   frame = audioQueue.front().get()->buffer.get();
  //   int len = audioQueue.front().get()->bufferLen;
  //   outputFile.write(reinterpret_cast<const char *>(frame), len);
  //   audioQueue.pop();
  //   // int ret = audioFrameSender->sendAudioPcmData(frameBuf, 0, samplesPer10ms, agora::rtc::TWO_BYTES_PER_SAMPLE,
  //   //                                              options.audio.numOfChannels,
  //   //                                              options.audio.sampleRate);

  //   // if (ret < 0)
  //   // {
  //   //   AG_LOG(ERROR, "Failed to send audio frame!");
  //   // }
  // }
}

static void sendOneH264Frame(
    int frameRate, std::shared_ptr<HelperH264Frame> h264Frame,
    agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> videoH264FrameSender)
{
  agora::rtc::EncodedVideoFrameInfo videoEncodedFrameInfo;
  videoEncodedFrameInfo.rotation = agora::rtc::VIDEO_ORIENTATION_0;
  videoEncodedFrameInfo.codecType = agora::rtc::VIDEO_CODEC_H264;
  videoEncodedFrameInfo.framesPerSecond = frameRate;
  videoEncodedFrameInfo.frameType =
      (h264Frame.get()->isKeyFrame ? agora::rtc::VIDEO_FRAME_TYPE::VIDEO_FRAME_TYPE_KEY_FRAME
                                   : agora::rtc::VIDEO_FRAME_TYPE::VIDEO_FRAME_TYPE_DELTA_FRAME);

  // AG_LOG(DEBUG, "sendEncodedVideoImage, buffer %p, len %d, frameType %d",
  //        reinterpret_cast<uint8_t *>(h264Frame.get()->buffer.get()), h264Frame.get()->bufferLen,
  //        videoEncodedFrameInfo.frameType);

  videoH264FrameSender->sendEncodedVideoImage(
      reinterpret_cast<uint8_t *>(h264Frame.get()->buffer.get()), h264Frame.get()->bufferLen,
      videoEncodedFrameInfo);
}

static void SampleSendAudioTask(
    const SampleOptions &options,
    agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender, bool &exitFlag, bool &sendAudioFlag)
{
  // Currently only 10 ms PCM frame is supported. So PCM frames are sent at 10 ms interval
  PacerInfo pacer = {0, 10, 0, std::chrono::steady_clock::now()};

  // 打开输出文件
  // if (!micAudioFile.is_open())
  // {
  //   printf("Could not open output file.");
  //   return;
  // }
  // if (!playAudioFile.is_open())
  // {
  //   printf("Could not open output file.");
  //   return;
  // }

  while (!exitFlag && sendAudioFlag)
  {
    // if (startCancellation)
    // {
    //   sendOnePcmFrame(options, audioFrameSender);
    //   waitBeforeNextSend(pacer); // sleep for a while before sending next frame
    // }
    // else
    // {
    //   usleep(1000);
    // }
    sendOnePcmFrame(options, audioFrameSender);
    waitBeforeNextSend(pacer); // sleep for a while before sending next frame
  }
  // micAudioFile.close();
  playAudioFile.close();
  cleanAudioFile.close();
}

static void SampleSendVideoH264Task(
    const SampleOptions &options,
    agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> videoH264FrameSender,
    bool &exitFlag)
{
  // std::unique_ptr<HelperH264FileParser> h264FileParser(
  //     new HelperH264FileParser(options.videoFile.c_str()));
  // h264FileParser->initialize();

  // Calculate send interval based on frame rate. H264 frames are sent at this interval
  PacerInfo pacer = {0, 1000 / options.video.frameRate, 0, std::chrono::steady_clock::now()};

  while (!exitFlag)
  {
    // if (auto h264Frame = h264FileParser->getH264Frame()) {
    if (!videoQueue.empty())
    {
      auto h264Frame = videoQueue.front();
      videoQueue.pop();
      sendOneH264Frame(options.video.frameRate, h264Frame, videoH264FrameSender);
      waitBeforeNextSend(pacer); // sleep for a while before sending next frame
    }
    else
    {
      usleep(1 * 1000);
    }
    // }
  };
}

static void SignalHandler(int sigNo)
{
  exitFlag = true;
}

// 回调函数，用于处理服务器响应的数据
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
  size_t totalSize = size * nmemb;
  userp->append((char *)contents, totalSize);
  return totalSize;
}

using namespace agora::tools;

void connect_callback(struct mosquitto *mosq, void *obj, int result)
{
  if (!result)
  {
    mosquitto_subscribe(mosq, NULL, "wakeup/text", 1);
    mosquitto_subscribe(mosq, NULL, "wakeup/enable", 1);
  }
}

void ttsOnLine(string msg)
{
  if (agent_id.empty())
  {
    return;
  }

  // 初始化CURL会话
  curl = curl_easy_init();
  if (curl)
  {
    string url = "https://api.agora.io/cn/api/conversational-ai-agent/v2/projects/" + string(config.app_id) + "/agents/" + string(agent_id) + "/speak";
    // 设置URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // 设置POST请求类型
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // 设置POST参数，这里使用一个结构体数组模拟JSON数据，实际应用中可以根据需要修改为具体的字符串格式
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json"); // 设置头部信息
    headers = curl_slist_append(headers, string("Authorization: Basic " + string(config.authorization)).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); // 应用头部信息

    std::string postData = R"({
      "text": ")" + msg +
                           R"(",
      "priority": "INTERRUPT",
      "interruptable": true
    })";

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str()); // 设置POST字段数据
    // AG_LOG(INFO, "json:%s", postData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.length()); // 使用字符串的实际长度
    // 设置回调函数，用于接收返回的数据
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer); // 将返回的数据传递给readBuffer

    // 执行请求并获取结果
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    else
    {
      // std::cout << "Response: " << readBuffer << std::endl; // 输出返回的数据
      std::cout << "tts 播放成功" << std::endl;
    }
    readBuffer = "";
    curl_slist_free_all(headers); // 释放头部信息列表内存
    curl_easy_cleanup(curl);      // 清理CURL会话资源
  }
}

void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
  std::cout << "Received: " << msg->topic << " -> " << (char *)msg->payload << std::endl;
  if (strcmp(msg->topic, "wakeup/enable") == 0)
  {
    // 清空采集音队列
    while (!micAudioQueue.empty())
      micAudioQueue.pop();
    // 1 离线语音、2、在线语音 3、关闭在线播放 4、开启ai对话
    if (strcmp((char *)msg->payload, "1") == 0)
    {
      if (!initAiFlag)
      {
        publishMessage(mosq, "1", "alsa/enable");
      }
      // captureAudioFlag = false;
      // if (captureAudioThread.joinable())
      // {
      //   captureAudioThread.join();
      // }
      while (!micAudioQueue.empty())
        micAudioQueue.pop();
    }
    else if (strcmp((char *)msg->payload, "2") == 0)
    {
      if (!initAiFlag)
      {
        publishMessage(mosq, "1", "alsa/enable");
      }
      // captureAudioFlag = true;
      // if (!captureAudioThread.joinable())
      // {
      //   captureAudioThread = std::thread(startCaptrueAudio, std::ref(config), audioCallback, std::ref(exitFlag), std::ref(captureAudioFlag));
      // }
      while (!micAudioQueue.empty())
        micAudioQueue.pop();
    }
    else if (strcmp((char *)msg->payload, "3") == 0)
    {
      while (!micAudioQueue.empty())
        micAudioQueue.pop();
      destory_ai_conversation(0, false);
      printf("关闭播放线程.\n");
      // playAudioFlag = false;
      // if (playAudioThread.joinable())
      // {
      //   playAudioThread.join();
      //   printf("关闭播放线程.\n");
      // }
    }
    else if (strcmp((char *)msg->payload, "4") == 0)
    {
      if (agent_id.empty())
      {
        while (!micAudioQueue.empty())
          micAudioQueue.pop();
        while (!sendAudioQueue.empty())
          sendAudioQueue.pop();
        if (destoryThread.joinable())
          destoryThread.join();
        if (playAudioThread.joinable())
          playAudioThread.join();
        if (sendAudioThread.joinable())
          sendAudioThread.join();
        if (!captureAudioFlag && captureAudioThread.joinable())
          captureAudioThread.join();
        sleep(2);
        playAudioFlag = true;
        captureAudioFlag = true;
        sendAudioFlag = true;
        sendAudioThread = std::thread(SampleSendAudioTask, options, audioFrameSender, std::ref(exitFlag), std::ref(sendAudioFlag));
        playAudioThread = std::thread(startPlayAudio, std::ref(config), DEFAULT_NUM_OF_CHANNELS, DEFAULT_SAMPLE_RATE, std::ref(exitFlag), std::ref(playAudioFlag));
        init_ai_conversation();
        sleep(2);
        if (!captureAudioThread.joinable())
          captureAudioThread = std::thread(startCaptrueAudio, std::ref(config), audioCallback, std::ref(exitFlag), std::ref(captureAudioFlag));
        printf("agent_id:%s\n", agent_id.c_str());
      }
    }
    return;
  }
  if (strcmp(msg->topic, "wakeup/text") == 0 && !agent_id.empty())
  {

    // audio_espeak_play(VOICES_WITH_MAN_CN, (char *)msg->payload);
    // // audio_espeak_play(VOICES_WITH_MAN_CN, "你好");
    // return;
    ttsOnLine(string((char *)msg->payload));
  }
}

static int handler(void *user, const char *section, const char *name,
                   const char *value)
{
  Configuration *pconfig = (Configuration *)user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
  if (MATCH("user", "version"))
  {
    pconfig->version = atoi(value);
  }
  else if (MATCH("user", "app_id"))
  {
    pconfig->app_id = strdup(value);
  }
  else if (MATCH("user", "channel_name"))
  {
    pconfig->channel_name = strdup(value);
  }
  else if (MATCH("user", "app_certificate"))
  {
    pconfig->app_certificate = strdup(value);
  }
  else if (MATCH("user", "authorization"))
  {
    pconfig->authorization = strdup(value);
  }
  else if (MATCH("user", "audio_dev"))
  {
    pconfig->audio_dev = strdup(value);
  }
  else if (MATCH("user", "play_audio_dev"))
  {
    pconfig->play_audio_dev = strdup(value);
  }
  else
  {
    return 0; /* unknown section/name, error */
  }
  return 1;
}

std::string get_executable_path()
{
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len != -1)
  {
    buf[len] = '\0';
    return std::string(buf);
  }
  else
  {
    perror("readlink");
    return "";
  }
}

std::string get_executable_directory()
{
  std::string path = get_executable_path();
  if (!path.empty())
  {
    char dir[PATH_MAX];
    // 使用 dirname
    char *result = dirname(dir); // 注意：dirname 会修改 dir 的内容
    if (result != NULL)
    {
      return std::string(result);
    }
  }
  return "";
}

void init_ai_conversation()
{
  // 初始化CURL会话
  curl = curl_easy_init();
  if (curl)
  {
    string url = "https://api.sd-rtn.com/cn/api/conversational-ai-agent/v2/projects/" + string(config.app_id) + "/join";
    // 设置URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // 设置POST请求类型
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // 设置POST参数，这里使用一个结构体数组模拟JSON数据，实际应用中可以根据需要修改为具体的字符串格式
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json"); // 设置头部信息
    headers = curl_slist_append(headers, string("Authorization: Basic " + string(config.authorization)).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); // 应用头部信息
    //  "interrupt_keywords":["等一下","停止","暂停","等一等","等会儿"]
    /*
     "create_response":true,
       "interrupt_response":true,
       "eagerness":"auto",
       "interrupt_keywords": ["等一下","等会儿","等一等","停止","暂停"]
    */
    // 设置POST参数的JSON字符串
    // BV002_streaming
    std::string postData = "{\"name\": \"" + options.channelId +
                           "\",\"properties\": {" +
                           "\"channel\":\"" + options.channelId + "\"," +
                           R"(
       "agent_rtc_uid": "0",
       "turn_detection": {
       "interrupt_mode": "ignore"
      
       },
       "remote_rtc_uids": [
         "*"
       ],
       "enable_string_uid": true,
       "idle_timeout": 60,
       "llm": {
         "url": "https://ark.cn-beijing.volces.com/api/v3/chat/completions",
         "api_key": "93902024-64db-4e5c-b050-41d3da0e5765",
         "max_history": 5,
         "system_messages": [
           {
             "role": "system",
             "content": "你的名字叫GO Mate Mini,是由广汽集团开发的智能人形机器人,回答问题使用粤语语气。回答问题最多不超50个汉字。收到不会回答或者不清楚的问题,不要回答任何内容。回答问题的时候不会被新问题打断,忽略新问题,直到当前问题回答完成。"
           }
         ],
         "params": {
           "model": "doubao-1-5-lite-32k-250115",
           "max_token": 125
         },
         "greeting_message": "你好呀,有什么可以帮您！",
         "failure_message": "我出错了，请稍等！"
       },
       "asr": {
                "vendor": "tencent",
                "language": "zh-CN",
                "vendor_model": "16k_yue"
       },
       "vad": {
         "interrupt_duration_ms": 300,
         "prefix_padding_ms": 300,
         "silence_duration_ms": 480,
         "threshold": 0.5
       },
       "tts": {
         "vendor": "bytedance",
         "params": {
           "token": "j2ncoUOAg7y9E91LWgBnJqc-bNJfuWi4",
           "app_id": "4331214271",
           "cluster": "volcano_tts",
           "voice_type": "BV026_streaming",
           "speed_ratio": 1,
           "volume_ratio": 3,
           "pitch_ratio": 1,
           "emotion": "happy"
         }
       },
       "parameters": {
         "transcript": {
           "enable": true,
           "protocol_version": "v2",
           "enable_words": false,
           "redundant": false
         },
         "enable_metrics": true,
         "audio_scenario": "default",
         "enable_dump": true
       },
       "token": ")" + options.appId +
                           "\"," +
                           "\"advanced_features\": {" +
                           "\"enable_aivad\": false" +
                           "}" +
                           "}" +
                           "}";
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str()); // 设置POST字段数据
    AG_LOG(INFO, "json:%s", postData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.length() + 1); // 使用字符串的实际长度

    // 设置回调函数，用于接收返回的数据
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer); // 将返回的数据传递给readBuffer
    auto start = std::chrono::system_clock::now();
    printf("请求智能体\n");
    // 执行请求并获取结果
    res = curl_easy_perform(curl);
    auto end = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    cout << "创建智能体消耗时间:" << ts << std::endl;
    if (res != CURLE_OK)
    {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
      readBuffer = "";
      // 清理头部信息列表和CURL会话
      curl_slist_free_all(headers); // 释放头部信息列表内存
      curl_easy_cleanup(curl);      // 清理CURL会话资源
      destory_ai_conversation(0, true);
      publishMessage(mosq, "3", "alsa/enable");
      return;
    }
    else
    {
      try
      {
        std::cout << "Response: " << readBuffer << std::endl; // 输出返回的数据
        json j = json::parse(readBuffer);
        if (!j.contains("agent_id"))
        {
          printf("请求智能体失败\n");
          destory_ai_conversation(0, true);
          publishMessage(mosq, "3", "alsa/enable");
        }
        else
        {
          agent_id = j["agent_id"];
          initAiFlag = true;
        }
      }
      catch (const std::exception &e)
      {
        std::cerr << "Standard exception caught: " << e.what() << std::endl;
        destory_ai_conversation(0, true);
        publishMessage(mosq, "3", "alsa/enable");
      }

      // std::string status = j["status"];
      // if (status.compare("IDLE"))
      // {
      //   status = "空闲状态";
      // }
      // else if (status.compare("STARTING"))
      // {
      //   status = "正在启动";
      // }
      // else if (status.compare("RUNNING"))
      // {
      //   status = "正在运行";
      // }
      // else if (status.compare("STOPPING"))
      // {
      //   status = "正在停止";
      // }
      // else if (status.compare("STOPPED"))
      // {
      //   status = "已退出";
      // }
      // else if (status.compare("RECOVERING"))
      // {
      //   status = "正在恢复";
      // }
      // else if (status.compare("FAILED"))
      // {
      //   status = "执行失败";
      // }
      // publishMessage(mosq, status.c_str());
    }
    readBuffer = "";
    // 清理头部信息列表和CURL会话
    curl_slist_free_all(headers); // 释放头部信息列表内存
    curl_easy_cleanup(curl);      // 清理CURL会话资源
  }
}

void destory_ai_conversation(int delay, bool iswakeup)
{
  // 初始化CURL会话
  curl = curl_easy_init();
  if (curl && !agent_id.empty())
  {
    string url = "https://api.agora.io/cn/api/conversational-ai-agent/v2/projects/" + string(config.app_id) + "/agents/" + string(agent_id) + "/leave";
    // 设置URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // 设置POST请求类型
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // 设置POST参数，这里使用一个结构体数组模拟JSON数据，实际应用中可以根据需要修改为具体的字符串格式
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json"); // 设置头部信息
    headers = curl_slist_append(headers, string("Authorization: Basic " + string(config.authorization)).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); // 应用头部信息

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL); // 设置POST字段数据
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0); // 使用字符串的实际长度

    // 设置回调函数，用于接收返回的数据
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer); // 将返回的数据传递给readBuffer

    sleep(delay);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    else
    {
      std::cout << "Response: " << readBuffer << std::endl; // 输出返回的数据
      std::cout << "会话销毁成功" << std::endl;
    }
    curl_slist_free_all(headers); // 释放头部信息列表内存
    curl_easy_cleanup(curl);      // 清理CURL会话资源
  }
  agent_id = "";
  readBuffer = "";
  time_stamp = 0;
  captureAudioFlag = false;
  playAudioFlag = false;
  sendAudioFlag = false;
  initAiFlag = false;
  if (iswakeup)
  {
    if (captureAudioThread.joinable())
    {
      printf("等待捕捉线程结束\n");
      captureAudioThread.join();
    }

    publishMessage(mosq, "1", "alsa/enable");
    printf("发送消息启动alsa\n");
  }

  printf("销毁会话\n");
}

void vad_audio()
{
  while (!exitFlag)
  {
    if (vad_buff != NULL)
    {
      int is_speech = speex_preprocess_run(preprocess, (short *)vad_buff);
      if (is_speech == 0)
      {
        if (time_stamp == 0)
        {
          printf("静音开始\n");
          time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }
      }
      else if (is_speech != 0)
      {
        if (time_stamp != 0)
        {
          time_stamp = 0;
          printf("静音结束\n");
        }
      }
      free(vad_buff);
      vad_buff = NULL;
    }
    else
    {
      usleep(80 * 1000);
      // std::cout << "vad_buff 为空" << std::endl;
    }
    auto now = std::chrono::system_clock::now();
    if (time_stamp != 0 && (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - time_stamp) > 20 * 1000)
    {
      if (!destoryThread.joinable())
      {
        ttsOnLine(string("智能模式已退出"));
        destoryThread = thread(destory_ai_conversation, 2, true);
      }

      // destory_ai_conversation();
      // return true;
    }
  }
}

int main(int argc, char *argv[])
{

  std::signal(SIGQUIT, SignalHandler);
  std::signal(SIGABRT, SignalHandler);
  std::signal(SIGINT, SignalHandler);
  // 解析配置文件
  std::string config_path(get_executable_directory() + "/config.ini");
  std::string config_path2(get_executable_directory() + "/../config.ini");
  if (ini_parse(config_path.c_str(), handler, &config) < 0)
  {
    if (ini_parse(config_path2.c_str(), handler, &config) < 0)
    {
      printf("Can't load 'config.ini'\n");
      return 1;
    }
  }
  // --------------------------mqtt创建--------------------------------
  mosquitto_lib_init();
  mosq = mosquitto_new("cpp_client", true, nullptr);
  mosquitto_connect_callback_set(mosq, connect_callback);
  mosquitto_message_callback_set(mosq, message_callback);

  if (mosquitto_connect(mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS)
  {
    std::cerr << "Failed to connect!" << std::endl;
    return 1;
  }

  mosquitto_loop_start(mosq);
  //--------------------------mqtt创建结束--------------------------------
  // audio_espeak_dev_init();
  // options.userId = "1";
  opt_parser optParser;
  optParser.add_long_opt("token", &options.appId, "The token for authentication / must");
  optParser.add_long_opt("channelId", &options.channelId, "Channel Id / must");
  optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
  optParser.add_long_opt("remoteUserId", &options.remoteUserId,
                         "The remote user to receive stream from");
  // optParser.add_long_opt("audioFile", &options.audioFile,
  //                        "The audio file in raw PCM format to be sent");
  // optParser.add_long_opt("videoFile", &options.videoFile,
  //                        "The video file in YUV420 format to be sent");
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Sample rate for the PCM file to be sent");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Number of channels for the PCM file to be sent");
  optParser.add_long_opt("fps", &options.video.frameRate,
                         "Target frame rate for sending the video stream");
  optParser.add_long_opt("bwe", &options.video.showBandwidthEstimation,
                         "show or hide bandwidth estimation info");
  optParser.add_long_opt("localIP", &options.localIP,
                         "Local IP");

  // if ((argc <= 1) || !optParser.parse_opts(argc, argv))
  // {
  //   std::ostringstream strStream;
  //   optParser.print_usage(argv[0], strStream);
  //   std::cout << strStream.str() << std::endl;
  //   return -1;
  // }
  optParser.parse_opts(argc, argv);
  // -------------------------------------------------------------------

  std::string channel_name = "test_gac_cname";
  if (options.channelId.empty())
  {
    // AG_LOG(ERROR, "Must provide channelId!");
    // return -1;
    if (strcmp(config.channel_name, "") != 0)
    {
      channel_name = config.channel_name;
    }
    options.channelId = channel_name;
  }

  int expiration_time = 3600 * 24;
  uint32_t uid = 1;
  std::string account = options.userId;
  uint32_t token_expiration_in_seconds = expiration_time;
  uint32_t privilege_expiration_in_seconds = expiration_time;
  uint32_t join_channel_privilege_expiration_in_seconds = expiration_time;
  uint32_t pub_audio_privilege_expiration_in_seconds = expiration_time;
  uint32_t pub_video_privilege_expiration_in_seconds = expiration_time;
  uint32_t pub_data_stream_privilege_expiration_in_seconds = expiration_time;
  std::string token;
  token = RtcTokenBuilder2::BuildTokenWithUserAccount(config.app_id, config.app_certificate, channel_name, account, UserRole::kRolePublisher, token_expiration_in_seconds,
                                                      privilege_expiration_in_seconds);
  // -------------------------------------------------------------------
  if (options.appId.empty())
  {
    // AG_LOG(ERROR, "Must provide appId!");
    // return -1;
    options.appId = token;
  }
  std::cout << "Token With Int Uid:" << options.appId << std::endl;

  // Create Agora service
  auto service = createAndInitAgoraService(false, true, true);
  if (!service)
  {
    AG_LOG(ERROR, "Failed to creating Agora service!");
  }

  // Create Agora connection
  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
  agora::agora_refptr<agora::rtc::IRtcConnection> connection = service->createRtcConnection(ccfg);
  if (!connection)
  {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }

  if (!options.localIP.empty())
  {
    if (setLocalIP(connection, options.localIP))
    {
      AG_LOG(ERROR, "set local IP to %s error!", options.localIP.c_str());
      return -1;
    }
  }

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

  // Register network observer to monitor bandwidth estimation result
  if (options.video.showBandwidthEstimation)
  {
    connection->registerNetworkObserver(connObserver.get());
  }

  // Create local user observer to monitor intra frame request
  auto localUserObserver = std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

  // Register audio frame observer to receive audio stream
  auto pcmFrameObserver = std::make_shared<PcmFrameObserver>(options.audioFile);
  if (connection->getLocalUser()->setPlaybackAudioFrameBeforeMixingParameters(
          options.audio.numOfChannels, options.audio.sampleRate))
  {
    AG_LOG(ERROR, "Failed to set audio frame parameters!");
    return -1;
  }
  localUserObserver->setAudioFrameObserver(pcmFrameObserver.get());

  // Connect to Agora channel
  if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
                          options.userId.c_str()))
  {
    AG_LOG(ERROR, "Failed to connect to Agora channel!");
    return -1;
  }

  // Create media node factory
  agora::agora_refptr<agora::rtc::IMediaNodeFactory> factory = service->createMediaNodeFactory();
  if (!factory)
  {
    AG_LOG(ERROR, "Failed to create media node factory!");
  }

  // Create audio data sender
  // agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender =
  //     factory->createAudioPcmDataSender();
  // if (!audioFrameSender)
  // {
  //   AG_LOG(ERROR, "Failed to create audio data sender!");
  //   return -1;
  // }

  audioFrameSender =
      factory->createAudioPcmDataSender();
  if (!audioFrameSender)
  {
    AG_LOG(ERROR, "Failed to create audio data sender!");
    return -1;
  }

  // Create audio track
  agora::agora_refptr<agora::rtc::ILocalAudioTrack> customAudioTrack =
      service->createCustomAudioTrack(audioFrameSender);
  if (!customAudioTrack)
  {
    AG_LOG(ERROR, "Failed to create audio track!");
    return -1;
  }

  // Create video frame sender
  agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> videoFrameSender =
      factory->createVideoEncodedImageSender();
  if (!videoFrameSender)
  {
    AG_LOG(ERROR, "Failed to create video frame sender!");
    return -1;
  }

  agora::rtc::SenderOptions option;
  option.ccMode = agora::rtc::TCcMode::CC_ENABLED;
  // Create video track
  agora::agora_refptr<agora::rtc::ILocalVideoTrack> customVideoTrack =
      service->createCustomVideoTrack(videoFrameSender, option);
  if (!customVideoTrack)
  {
    AG_LOG(ERROR, "Failed to create video track!");
    return -1;
  }

  // Publish audio & video track
  connection->getLocalUser()->publishAudio(customAudioTrack);
  connection->getLocalUser()->publishVideo(customVideoTrack);

  // Wait until connected before sending media stream
  connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS);

  if (!options.localIP.empty())
  {
    std::string ip;
    getLocalIP(connection, ip);
    AG_LOG(INFO, "Local IP:%s", ip.c_str());
  }

  // Start sending media data
  AG_LOG(INFO, "Start sending audio & video data ...");
  // std::thread sendVideoThread(SampleSendVideoH264Task, options, videoFrameSender,
  //                             std::ref(exitFlag));
  // std::thread captureVideoThread(startCaptrueVideo, videoCallback, std::ref(exitFlag));
  // std::thread sendAudioThread(SampleSendAudioTask, options, audioFrameSender, std::ref(exitFlag));
  // std::thread captureAudioThread(startCaptrueAudio, std::ref(config), audioCallback, std::ref(exitFlag), std::ref(enableAudioFlag));
  // playAudioThread = std::thread(startPlayAudio, DEFAULT_NUM_OF_CHANNELS, DEFAULT_SAMPLE_RATE, std::ref(exitFlag), std::ref(openSpeakFlag));

  vadThread = std::thread(vad_audio);

  // Subcribe streams from all remote users or specific remote user
  if (options.remoteUserId.empty())
  {
    AG_LOG(INFO, "Subscribe streams from all remote users");
    connection->getLocalUser()->subscribeAllAudio();
  }
  else
  {
    connection->getLocalUser()->subscribeAudio(options.remoteUserId.c_str());
  }

  //--------------------初始化回声消除---------------------------
  // 初始化回声消除器
  echo_state = speex_echo_state_init(FRAME_SIZE, FILTER_LEN);
  int param = SAMPLE_RATE;
  speex_echo_ctl(echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &param);
  // 初始化预处理器（降噪）
  preprocess = speex_preprocess_state_init(FRAME_SIZE, SAMPLE_RATE);
  // 关联回声消除器和预处理器
  // speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_ECHO_STATE, echo_state);
  // 启用降噪
  int denoise = 1;
  speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
  // 设置噪声抑制强度（例如 -25 dB）
  int noise_suppress = -25;
  speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise_suppress);
  // 1表示启用VAD
  int vad = 1;
  int prob_start = 20;    // 从静音到语音的转换概率阈值
  int prob_continue = 65; // 从语音到静音的转换概率阈值
  speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_VAD, &vad);
  speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_PROB_START, &prob_start);
  speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_PROB_CONTINUE, &prob_continue);
  // 启用自动增益控制
  int agc = 1;
  speex_preprocess_ctl(preprocess, SPEEX_PREPROCESS_SET_AGC, &agc);
  //--------------------回声消除结束-----------------------------
  publishMessage(mosq, "2", "alsa/enable");
  while (!exitFlag)
  {
    // usleep(100 * 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  destory_ai_conversation(0, true);
  if (vadThread.joinable())
    vadThread.join();
  if (captureAudioThread.joinable())
    captureAudioThread.join();
  if (playAudioThread.joinable())
    playAudioThread.join();
  if (sendAudioThread.joinable())
    sendAudioThread.join();
  if (destoryThread.joinable())
    destoryThread.join();
  std::cout << "线程释放完毕" << std::endl;
  // 释放资源
  speex_echo_state_destroy(echo_state);
  speex_preprocess_state_destroy(preprocess);
  // Unpublish audio & video track
  connection->getLocalUser()->unpublishAudio(customAudioTrack);
  connection->getLocalUser()->unpublishVideo(customVideoTrack);

  localUserObserver->unsetAudioFrameObserver();

  // Unregister connection observer
  connection->unregisterObserver(connObserver.get());

  // Unregister network observer
  connection->unregisterNetworkObserver(connObserver.get());

  // Disconnect from Agora channel
  if (connection->disconnect())
  {
    AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
    return -1;
  }
  AG_LOG(INFO, "Disconnected from Agora channel successfully");

  // Destroy Agora connection and related resources
  connObserver.reset();
  localUserObserver.reset();
  audioFrameSender = nullptr;
  videoFrameSender = nullptr;
  customAudioTrack = nullptr;
  customVideoTrack = nullptr;
  factory = nullptr;
  connection = nullptr;
  // Destroy Agora Service
  service->release();
  service = nullptr;
  // mqtt release
  mosquitto_loop_stop(mosq, true);
  mosquitto_destroy(mosq);
  mosquitto_lib_cleanup();
  std::cout << "释放mqtt完毕" << std::endl;

  return 0;
}

// 发布消息
void publishMessage(mosquitto *mosq, const char *message, const char *topic)
{
  int qos = 1;         // 服务质量等级（0, 1, 或 2）
  bool retain = false; // 是否保留消息
  // 发布启动失败消息publishMessage
  int ret = mosquitto_publish(mosq, nullptr, topic, strlen(message), message, qos, retain);
  if (ret != MOSQ_ERR_SUCCESS)
  {
    std::cerr << "Error: Failed to publish message." << std::endl;
  }
  else
  {
    std::cout << "enable alsa published successfully." << std::endl;
  }
}

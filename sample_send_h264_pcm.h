#pragma once
#define DEFAULT_CONNECT_TIMEOUT_MS (3000)
#define DEFAULT_SAMPLE_RATE (16000)
#define DEFAULT_NUM_OF_CHANNELS (1)
#define DEFAULT_FRAME_RATE (30)
// #define DEFAULT_AUDIO_FILE "../send_audio_16k_1ch.pcm"
#define DEFAULT_AUDIO_FILE "../send_audio_16k_1ch.pcm"
#define DEFAULT_VIDEO_FILE "../send_video.h264"

#define FRAME_SIZE 160    // 10ms帧（16kHz采样率）
#define SAMPLE_RATE 16000 // 采样率
#define FILTER_LEN 1024   // 尾音长度（300ms）

struct SampleOptions
{
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string remoteUserId;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  std::string videoFile = DEFAULT_VIDEO_FILE;
  std::string localIP;
  struct
  {
    int sampleRate = DEFAULT_SAMPLE_RATE;
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
  } audio;
  struct
  {
    int frameRate = DEFAULT_FRAME_RATE;
    bool showBandwidthEstimation = false;
  } video;
};

SampleOptions options;

void publishMessage(mosquitto *mosq);
void init_ai_conversation();
void destory_ai_conversation(int delay,bool iswakeup);
void ttsOnLine(string msg);
void vad_audio();

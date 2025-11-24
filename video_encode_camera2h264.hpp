#ifndef VIDEO_ENCODE_CAMERA2H264_H
#define VIDEO_ENCODE_CAMERA2H264_H
#include <memory>
#include <queue>

using namespace std;

typedef void (*AudioCallback)(u_int8_t *data, int len);
typedef void (*VideoCallback)(u_int8_t *data, int len,bool isKeyFrame);

typedef struct
{
  int version;
  const char *app_id;
  const char *app_certificate;
  const char *authorization;
  const char *channel_name;
  const char *audio_dev;
  const char *play_audio_dev;
} Configuration;

void startCaptrueAudio(Configuration &config,AudioCallback audioCallback,
    bool &exitFlag,bool &enableFlag);
void startCaptrueVideo(VideoCallback videoCallback,
    bool &exitFlag);
void startPlayAudio(Configuration &config,int channel,int samples,
    bool &exitFlag,bool &openSpeakFlag);
void pushPCM(uint8_t *pcm_data, int pcm_size);
#endif

//  Agora RTC/MEDIA SDK
//
//  Created by Jay Zhang in 2020-06.
//  Copyright (c) 2019 Agora.io. All rights reserved.
//

#include "sample_common.h"

#include "log.h"
#include <iostream>
#include <string>
#include <fstream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "AgoraBase.h"
#include "NGIAgoraRtcConnection.h"
#include "NGIAgoraLocalUser.h"

#if defined(TARGET_OS_LINUX)
#define DEFAULT_LOG_PATH ("./io.agora.rtc_sdk/agorasdk.log")
#else
#define DEFAULT_LOG_PATH ("/data/local/tmp/agorasdk.log")
#endif

#define DEFAULT_LOG_SIZE (20 * 1024 * 1024) // default log size is 512 kb

#define INTERNAL_RTC_KEY_LOCAL_IP "rtc.local.ip"
#define INTERNAL_RTC_KEY_RTC_UDP_SEND_FD "rtc.udp_send_fd"

// @WARNING : IAgoraService is Global singleton ！！Just create it once and make sure you don't destroy it before process end.
// @WARNING : Be careful when you Mult_thread programming！！
agora::base::IAgoraService *createAndInitAgoraService(bool enableAudioDevice,
													  bool enableAudioProcessor, bool enableVideo,
													  bool enableuseStringUid, bool enablelowDelay,
													  const char *appid)
{
	int32_t buildNum = 0;
	getAgoraSdkVersion(&buildNum);
#if defined(SDK_BUILD_NUM)
	if (buildNum != SDK_BUILD_NUM) {
		AG_LOG(ERROR, "SDK VERSION CHECK FAILED!\nSDK version: %d\nAPI Version: %d\n", buildNum,
			   SDK_BUILD_NUM);
		//return nullptr;
	}
#endif
	AG_LOG(INFO, "SDK version: %d\n", buildNum);
	auto service = createAgoraService();
	agora::base::AgoraServiceConfiguration scfg;
	scfg.appId = appid;
	scfg.enableAudioProcessor = enableAudioProcessor;
	scfg.enableAudioDevice = enableAudioDevice;
	scfg.enableVideo = enableVideo;
	scfg.useStringUid = enableuseStringUid;
	scfg.audioScenario = agora::rtc::AUDIO_SCENARIO_TYPE::AUDIO_SCENARIO_CHORUS;
	if (enablelowDelay) {
		scfg.channelProfile = agora::CHANNEL_PROFILE_TYPE::CHANNEL_PROFILE_CLOUD_GAMING;
	}
	if (service->initialize(scfg) != agora::ERR_OK) {
		return nullptr;
	}

	AG_LOG(INFO, "Created log file at %s", DEFAULT_LOG_PATH);
	if (service->setLogFile(DEFAULT_LOG_PATH, DEFAULT_LOG_SIZE) != 0) {
		return nullptr;
	}
	if (verifyLicense() != 0)
		return nullptr;
	return service;
}

class LicenseCallbackImpl : public agora::base::LicenseCallback {
public:
	LicenseCallbackImpl()
	{
	}
	virtual ~LicenseCallbackImpl()
	{
	}

	virtual void onCertificateRequired()
	{
		AG_LOG(INFO, "[%s, line: %d]", __FUNCTION__, __LINE__);
	}

	virtual void onLicenseRequest()
	{
		AG_LOG(INFO, "[%s, line: %d]", __FUNCTION__, __LINE__);
	}

	virtual void onLicenseValidated()
	{
		AG_LOG(INFO, "[%s, line: %d]", __FUNCTION__, __LINE__);
	}

	virtual void onLicenseError(int result)
	{
		AG_LOG(ERROR, "[%s, line: %d] result: %d", __FUNCTION__, __LINE__, result);
	}
};

static const std::string CERTIFICATE_FILE = "certificate.bin";

int verifyLicense()
{
#ifdef LICENSE_CHECK
	// Step1: read the certificate buffer from the certificate.bin file
	char *cert_buffer = NULL;
	int cert_length = 0;
	std::ifstream f_cert(CERTIFICATE_FILE.c_str(), std::ios::binary);
	if (f_cert) {
		f_cert.seekg(0, f_cert.end);
		cert_length = f_cert.tellg();
		f_cert.seekg(0, f_cert.beg);

		cert_buffer = new char[cert_length + 1];
		AG_LOG(INFO, "cert_length: %d", cert_length);
		memset(cert_buffer, 0, cert_length + 1);
		f_cert.read(cert_buffer, cert_length);
		if (!cert_buffer || f_cert.gcount() < cert_length) {
			f_cert.close();
			if (cert_buffer) {
				delete[] cert_buffer;
				cert_buffer = NULL;
			}
			AG_LOG(ERROR, "read %s failed", CERTIFICATE_FILE.c_str());
			return -1;
		} else {
			f_cert.close();
		}
	} else {
		AG_LOG(ERROR, "%s doesn't exist", CERTIFICATE_FILE.c_str());
		return -1;
	}
	AG_LOG(INFO, "certificate: %s", cert_buffer);

	// Step3: register callback of license state
	LicenseCallbackImpl *cb = static_cast<LicenseCallbackImpl *>(getAgoraLicenseCallback());
	if (!cb) {
		cb = new LicenseCallbackImpl();
		setAgoraLicenseCallback(static_cast<agora::base::LicenseCallback *>(cb));
	}

	// Step4: verify the license with credential and certificate
	int result = getAgoraCertificateVerifyResult(NULL, 0, cert_buffer, cert_length);
	AG_LOG(INFO, "verify result: %d", result);

	if (cert_buffer) {
		delete[] cert_buffer;
		cert_buffer = NULL;
	}

	return result;
#else
	return 0;
#endif
}

int32_t getLocalIP(agora::agora_refptr<agora::rtc::IRtcConnection> &connection, std::string &ip)
{
	int32_t retIntValue = -1;
	const char *intKey = INTERNAL_RTC_KEY_RTC_UDP_SEND_FD;
	agora::base::IAgoraParameter *agoraParameter = connection->getAgoraParameter();
	agoraParameter->getInt(intKey, retIntValue);
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if (getsockname(retIntValue, (struct sockaddr *)&addr, &len)) {
		perror("getsockname");
		return -1;
	}
	ip = std::string(inet_ntoa(addr.sin_addr));
	AG_LOG(INFO, "getLocalIP:%s", ip.c_str());
	return 0;
}

int32_t setLocalIP(agora::agora_refptr<agora::rtc::IRtcConnection> &connection,
				   const std::string &ip)
{
	agora::util::AString retStringValue;
	const char *stringKey = INTERNAL_RTC_KEY_LOCAL_IP;
	agora::base::IAgoraParameter *agoraParameter = connection->getAgoraParameter();
	agoraParameter->setString(stringKey, ip.c_str());
	agoraParameter->getString(stringKey, retStringValue);
	AG_LOG(INFO, "setLocalIP:%s", retStringValue->data());
	return 0;
}

int32_t setLowDelay(agora::agora_refptr<agora::rtc::IRtcConnection> &connection)
{
	auto s = connection->getAgoraParameter();
	int32_t ret = 0;
	connection->getLocalUser()->setAudioScenario(
			agora::rtc::AUDIO_SCENARIO_TYPE::AUDIO_SCENARIO_CHORUS);
	ret = s->setUInt("rtc.video.uplink_max_retry_times", 5);
	if (!ret) {
		AG_LOG(INFO, "[Low Delay] set the max video resend times %d successfully!", 5);
	} else {
		AG_LOG(INFO,
			   "[Low Delay] close video resend  fail! The "
			   "err num is %d",
			   ret);
	}

	ret = s->setUInt("rtc.cc_private", 16);
	ret = s->setUInt("rtc.remote_cc_private", 16);

	if (!ret) {
		AG_LOG(INFO, "[Low Delay] set the rtc.remote_cc_private %d successfully!", 16);
	} else {
		AG_LOG(INFO,
			   "[Low Delay] set rtc.remote_cc_private  fail! The "
			   "err num is %d",
			   ret);
	}

	ret = s->setInt("rtc.ack_delay", 0);
	ret = s->setInt("rtc.remote_ack_delay", 0);
	if (!ret) {
		AG_LOG(INFO, "[Low Delay] set the rtc.remote_ack_delay %d successfully!", 0);
	} else {
		AG_LOG(INFO,
			   "[Low Delay] set rtc.remote_ack_delay fail! The "
			   "err num is %d",
			   ret);
	}

	ret = s->setParameters(
			"{\"rtc.video.fec_protection_ratio_level\":\"000008888,000008888,000008888,000008885\"}");
	if (!ret) {
		AG_LOG(INFO, "[Low Delay] set the rtc.video.fec_protection_ratio_level successfully!");
	} else {
		AG_LOG(INFO,
			   "[Low Delay] set the rtc.video.fec_protection_ratio_level fail! The "
			   "err num is %d",
			   ret);
	}

	ret = s->setParameters("{\"rtc.paced_sender_enabled\": 0}");
	if (!ret) {
		AG_LOG(INFO, "[Low Delay] close the send pacer successfully!");
	} else {
		AG_LOG(INFO,
			   "[Low Delay] close the send pacer fail! The "
			   "err num is %d",
			   ret);
	}
	ret = s->setParameters("{\"rtc.enableMultipath\":true}");
	if (!ret) {
		AG_LOG(INFO, "[Low Delay] open multipath successfully!");
	} else {
		AG_LOG(INFO,
			   "[Low Delay] open multipath fail! The "
			   "err num is %d",
			   ret);
	}
	ret = s->setParameters("{\"rtc.path_scheduling_strategy\":0}");
	if (!ret) {
		AG_LOG(INFO, "[Low Delay]path_scheduling_strategy is 0!");
	} else {
		AG_LOG(INFO, "[Low Delay] path_scheduling_strategy is error %d", ret);
	}

	ret = s->setParameters("{\"engine.video.hw_encoder_provider\":\"nvmpi265\"}");
	if (!ret) {
		AG_LOG(INFO, "[Low Delay]HW is 0!");
	} else {
		AG_LOG(INFO, "[Low Delay] HW is error %d", ret);
	}

	ret = s->setParameters("{\"engine.video.enable_hw_encoder\":\"true\"}");
	if (!ret) {
		AG_LOG(INFO, "[Low Delay]HW is 0!");
	} else {
		AG_LOG(INFO, "[Low Delay] HW is error %d", ret);
	}
	ret = s->setUInt("che.video.video_min_framerate", 20);
	if (!ret) {
		AG_LOG(INFO, "[Low Delay]che.video.video_min_framerate is 20!");
	} else {
		AG_LOG(INFO, "[Low Delay] HW is error %d", ret);
	}
	//  JUST for local vos connect
	// s->setBool("rtc.force_local", true);
	// s->setBool("rtc.local_ap_low_level", true);

	// s->setBool("rtc.enable_nasa2", true);
	// s->setParameters("{\"rtc.vos_list\":[\"10.62.0.95:4701\"]}");
	// s->setParameters("{\"rtc.local_ap_list\":[\"10.62.0.95\"]}");
	return ret;
}
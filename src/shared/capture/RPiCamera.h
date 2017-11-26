/*
 * camera.h
 *
 *  Created on: 21.08.2017
 *      Author: AndreR
 */

#pragma once

#include "interface/mmal/mmal.h"

namespace rpi
{

enum CameraIso
{
  CAMERA_ISO_100,
  CAMERA_ISO_200,
  CAMERA_ISO_400,
  CAMERA_ISO_800,
};

enum CameraRotation
{
  CAMERA_ROTATE_0,
  CAMERA_ROTATE_90,
  CAMERA_ROTATE_180,
  CAMERA_ROTATE_270,
};

enum CameraMirror
{
  CAMERA_MIRROR_NONE,
  CAMERA_MIRROR_HORIZONTAL,
  CAMERA_MIRROR_VERTICAL,
  CAMERA_MIRROR_BOTH,
};

struct Resolution
{
  int32_t width;
  int32_t height;
};

class RPiCamera
{
public:
  RPiCamera();
  virtual ~RPiCamera();

  void start(int32_t width, int32_t height, int32_t framerate, MMAL_FOURCC_T encoding);
  void stop();

  MMAL_BUFFER_HEADER_T* waitForFrame(uint32_t timeoutMs);
  void releaseFrame(MMAL_BUFFER_HEADER_T* pBuf);

  int64_t getStcTimestampUs();
  const Resolution& getResolution() const { return camResolution_; }

  void setSaturation(int saturation); // -100 - 100
  void setSharpness(int sharpness); // -100 - 100
  void setContrast(int contrast); // -100 - 100
  void setBrightness(int brightness); // 0 - 100
  void setIso(CameraIso iso);
  void setMeteringMode(MMAL_PARAM_EXPOSUREMETERINGMODE_T mode);
  void setExposureCompensation(int expComp); // -10 - 10
  void setExposureMode(MMAL_PARAM_EXPOSUREMODE_T mode);
  void setAwbMode(MMAL_PARAM_AWBMODE_T awb_mode);
  void setAwbGains(float r_gain, float b_gain);
  void setRotation(CameraRotation rotation);
  void setMirror(CameraMirror flip);
  void setShutterSpeed(int speed_us);
  void setAlgorithmControl(MMAL_PARAMETER_ALGORITHM_CONTROL_ALGORITHMS_T algo, bool enable);
  void setUseCase(MMAL_PARAM_CAMERA_USE_CASE_T useCase);
  void setZeroShutterLag(bool enable);
  void setAnalogGain(float analog);
  void setDigitalGain(float digital);

  MMAL_PORT_T* getPreviewPort() { return pPreviewPort; }
  void _internalPreviewOutCb(MMAL_BUFFER_HEADER_T* pBuf);

private:
  void setupCamera();
  void setupStillPort();
  void setupVideoPort();
  void setupPreviewPort();
  void setDefaultParameters();

  void printPortEncodings(MMAL_PORT_T* pPort);

  Resolution camResolution_;
  uint16_t framerate_;
  MMAL_FOURCC_T encoding_;

  MMAL_COMPONENT_T* pCameraComponent;
  MMAL_POOL_T* pPreviewPool;
  MMAL_PORT_T* pPreviewPort;

  MMAL_QUEUE_T* pFrameQueue;
};

} // namespace rpi

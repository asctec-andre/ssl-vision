/*
 * camera.c
 *
 *  Created on: 21.08.2017
 *      Author: AndreR
 */

#include "RPiCamera.h"
#include <cstdio>
#include <stdexcept>

#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"
#include "interface/mmal/mmal_parameters_camera.h"

// Standard port setting for the camera component
#define MMAL_CAMERA_PREVIEW_PORT  0
#define MMAL_CAMERA_VIDEO_PORT    1
#define MMAL_CAMERA_CAPTURE_PORT  2

// Video render needs at least 2 buffers to get to 60fps. 3 buffers for 90fps.
#define VIDEO_OUTPUT_BUFFERS_NUM 3

namespace rpi
{

class mmal_error : public std::runtime_error
{
public:
  mmal_error(int code)
    :std::runtime_error(std::string("MMAL Error")), errorCode(code) {}

private:
  int errorCode;
};

static void controlCallback(MMAL_PORT_T* pPort, MMAL_BUFFER_HEADER_T* pBuffer)
{
  (void)pPort;
  mmal_buffer_header_release(pBuffer);
}

// MMAL Callback from camera preview output port.
static void previewOutputCallback(MMAL_PORT_T* pPort, MMAL_BUFFER_HEADER_T* pBuf)
{
  RPiCamera* pCamera = (RPiCamera*)pPort->userdata;

  if (pBuf->length == 0)
  {
    fprintf(stderr, "%s: zero-length buffer => EOS\n", pPort->name);
  }
  else if (pBuf->data == NULL)
  {
    fprintf(stderr, "%s: zero buffer handle\n", pPort->name);
  }
  else
  {
    pCamera->_internalPreviewOutCb(pBuf);
  }
}

void RPiCamera::_internalPreviewOutCb(MMAL_BUFFER_HEADER_T* pBuf)
{
  if(mmal_queue_length(pFrameQueue) < 2)
  {
    // only enqueue if there is space available
    mmal_queue_put(pFrameQueue, pBuf);
  }
  else
  {
    // release directly
    releaseFrame(pBuf);
  }
}

void RPiCamera::releaseFrame(MMAL_BUFFER_HEADER_T* pBuf)
{
  // release buffer back to the pool
  if(pBuf)
    mmal_buffer_header_release(pBuf);

  // and send one back to the port (if still open)
  if (pPreviewPool && pPreviewPort->is_enabled)
  {
    MMAL_BUFFER_HEADER_T* pNewBuf = mmal_queue_get(pPreviewPool->queue);

    if (pNewBuf)
    {
      MMAL_STATUS_T status = mmal_port_send_buffer(pPreviewPort, pNewBuf);
      if (status != MMAL_SUCCESS)
        fprintf(stderr, "Could not send buffer to port %d\n", status);
    }
    else
    {
      fprintf(stderr, "Could not get buffer from port queue\n");
    }
  }
}

int64_t RPiCamera::getStcTimestampUs()
{
  MMAL_PARAMETER_UINT64_T time = {{MMAL_PARAMETER_SYSTEM_TIME, sizeof(time)}, 0};

  if(mmal_port_parameter_get(pPreviewPort, &time.hdr) == MMAL_SUCCESS)
    return (int64_t)time.value;

  return 0;
}

MMAL_BUFFER_HEADER_T* RPiCamera::waitForFrame(uint32_t timeoutMs)
{
  if(!pFrameQueue)
    return 0;

  return mmal_queue_timedwait(pFrameQueue, timeoutMs);
}

RPiCamera::RPiCamera()
:framerate_(0)
{
  pCameraComponent = 0;
  pPreviewPool = 0;
  pPreviewPort = 0;
  pFrameQueue = 0;
  camResolution_.width = 0;
  camResolution_.height = 0;
}

RPiCamera::~RPiCamera()
{
  stop();

  if(pFrameQueue)
  {
    mmal_queue_destroy(pFrameQueue);
  }
}

void RPiCamera::setupCamera()
{
  // Create the camera component
  MMAL_STATUS_T status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &pCameraComponent);
  if (status != MMAL_SUCCESS)
  {
    fprintf(stderr, "Failed to create camera component\n");
    throw mmal_error(status);
  }

  // select camera
  MMAL_PARAMETER_INT32_T cameraNum = { { MMAL_PARAMETER_CAMERA_NUM, sizeof(cameraNum) }, 0 };
  status = mmal_port_parameter_set(pCameraComponent->control, &cameraNum.hdr);
  if (status != MMAL_SUCCESS)
  {
    fprintf(stderr, "Could not select camera : error %d\n", status);
    throw mmal_error(status);
  }
  if (!pCameraComponent->output_num)
  {
    status = MMAL_ENOSYS;
    fprintf(stderr, "Camera doesn't have output ports\n");
    throw mmal_error(status);
  }

  // Enable the camera, and tell it its control callback function
  status = mmal_port_enable(pCameraComponent->control, controlCallback);
  if (status != MMAL_SUCCESS)
  {
    fprintf(stderr, "Unable to enable control port : error %d\n", status);
    throw mmal_error(status);
  }

  //  set up the camera configuration
  MMAL_PARAMETER_CAMERA_CONFIG_T camConfig =
  {
    { MMAL_PARAMETER_CAMERA_CONFIG, sizeof(camConfig) },
    .max_stills_w = (uint32_t)camResolution_.width,
    .max_stills_h = (uint32_t)camResolution_.height,
    .stills_yuv422 = 0,
    .one_shot_stills = 1,
    .max_preview_video_w = (uint32_t)camResolution_.width,
    .max_preview_video_h = (uint32_t)camResolution_.height,
    .num_preview_video_frames = VIDEO_OUTPUT_BUFFERS_NUM,
    .stills_capture_circular_buffer_height = 0,
    .fast_preview_resume = 0,
    .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RAW_STC
  };

  status = mmal_port_parameter_set(pCameraComponent->control, &camConfig.hdr);
  if(status != MMAL_SUCCESS)
  {
    fprintf(stderr, "Unable to set camera config %d\n", status);
    throw mmal_error(status);
  }
}

void RPiCamera::setDefaultParameters()
{
//  setIso(ISO_100);
  setExposureMode(MMAL_PARAM_EXPOSUREMODE_OFF);
  setAwbMode(MMAL_PARAM_AWBMODE_OFF);

  setShutterSpeed(10000);
  setAwbGains(1.4f, 1.5f);
  setAnalogGain(2.0f);
  setDigitalGain(2.0f);
//  setExposureCompensation(0);
//  setAwbGains(1.8f, 1.0f);
  setSharpness(0);
  setContrast(0);
  setBrightness(50);
  setSaturation(0);

  setAlgorithmControl(MMAL_PARAMETER_ALGORITHM_CONTROL_ALGORITHMS_VIDEO_DENOISE, true);
  setUseCase(MMAL_PARAM_CAMERA_USE_CASE_VIDEO_CAPTURE);
  setZeroShutterLag(true);
}

void RPiCamera::setupStillPort()
{
  MMAL_PORT_T* pStillPort = pCameraComponent->output[MMAL_CAMERA_CAPTURE_PORT];
  MMAL_ES_FORMAT_T* pESFormat = pStillPort->format;

  pESFormat->encoding = MMAL_ENCODING_OPAQUE;
  pESFormat->es->video.width = VCOS_ALIGN_UP(camResolution_.width, 32);
  pESFormat->es->video.height = VCOS_ALIGN_UP(camResolution_.height, 16);
  pESFormat->es->video.crop.x = 0;
  pESFormat->es->video.crop.y = 0;
  pESFormat->es->video.crop.width = camResolution_.width;
  pESFormat->es->video.crop.height = camResolution_.height;
  pESFormat->es->video.frame_rate.num = 0;
  pESFormat->es->video.frame_rate.den = 1;

  MMAL_STATUS_T status = mmal_port_format_commit(pStillPort);
  if (status != MMAL_SUCCESS)
  {
    fprintf(stderr, "camera still format couldn't be set\n");
    throw mmal_error(status);
  }

  // Ensure there are enough buffers to avoid dropping frames
  pStillPort->buffer_num = pStillPort->buffer_num_recommended;
  pStillPort->buffer_size = pStillPort->buffer_size_recommended;
}

void RPiCamera::setupVideoPort()
{
  MMAL_PORT_T* pVideoPort = pCameraComponent->output[MMAL_CAMERA_VIDEO_PORT];
  MMAL_ES_FORMAT_T* pESFormat = pVideoPort->format;

  fprintf(stderr, "video supported encodings: ");
  printPortEncodings(pVideoPort);

  pESFormat->encoding = encoding_;
  pESFormat->encoding_variant = MMAL_ENCODING_VARIANT_DEFAULT;

  pESFormat->es->video.width = VCOS_ALIGN_UP(camResolution_.width, 32);
  pESFormat->es->video.height = VCOS_ALIGN_UP(camResolution_.height, 16);
  pESFormat->es->video.crop.x = 0;
  pESFormat->es->video.crop.y = 0;
  pESFormat->es->video.crop.width = camResolution_.width;
  pESFormat->es->video.crop.height = camResolution_.height;
  pESFormat->es->video.frame_rate.num = framerate_;
  pESFormat->es->video.frame_rate.den = 1;

  MMAL_STATUS_T status = mmal_port_format_commit(pVideoPort);
  if (status != MMAL_SUCCESS)
  {
    fprintf(stderr, "camera video port format couldn't be set\n");
    throw mmal_error(status);
  }

  pVideoPort->buffer_num = pVideoPort->buffer_num_recommended;
  pVideoPort->buffer_size = pVideoPort->buffer_size_recommended;
}

void RPiCamera::setupPreviewPort()
{
  pPreviewPort = pCameraComponent->output[MMAL_CAMERA_PREVIEW_PORT];
  MMAL_ES_FORMAT_T* pESFormat = pPreviewPort->format;

  fprintf(stderr, "preview supported encodings: ");
  printPortEncodings(pPreviewPort);

  pESFormat->encoding = encoding_;
  pESFormat->encoding_variant = MMAL_ENCODING_VARIANT_DEFAULT;

  pESFormat->es->video.width = VCOS_ALIGN_UP(camResolution_.width, 32);
  pESFormat->es->video.height = VCOS_ALIGN_UP(camResolution_.height, 16);
  pESFormat->es->video.crop.x = 0;
  pESFormat->es->video.crop.y = 0;
  pESFormat->es->video.crop.width = camResolution_.width;
  pESFormat->es->video.crop.height = camResolution_.height;
  pESFormat->es->video.frame_rate.num = framerate_;
  pESFormat->es->video.frame_rate.den = 1;

  MMAL_STATUS_T status = mmal_port_parameter_set_boolean(pPreviewPort, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
  if (status != MMAL_SUCCESS)
  {
    fprintf(stderr, "Failed to enable zero copy on camera preview port\n");
    throw mmal_error(status);
  }

  status = mmal_port_format_commit(pPreviewPort);
  if (status != MMAL_SUCCESS)
  {
    fprintf(stderr, "camera viewfinder format couldn't be set\n");
    throw mmal_error(status);
  }

  pPreviewPort->buffer_num = VIDEO_OUTPUT_BUFFERS_NUM;
  pPreviewPort->buffer_size = pPreviewPort->buffer_size_recommended;

  pPreviewPort->userdata = (struct MMAL_PORT_USERDATA_T*) this;
}

#define MAX_ENCODINGS_NUM 25
typedef struct {
   MMAL_PARAMETER_HEADER_T header;
   MMAL_FOURCC_T encodings[MAX_ENCODINGS_NUM];
} MMAL_SUPPORTED_ENCODINGS_T;

void RPiCamera::printPortEncodings(MMAL_PORT_T* pPort)
{
  MMAL_SUPPORTED_ENCODINGS_T sup_encodings = {{MMAL_PARAMETER_SUPPORTED_ENCODINGS, sizeof(sup_encodings)}, {0}};
  int ret = mmal_port_parameter_get(pPort, &sup_encodings.header);
  if(ret == MMAL_SUCCESS || ret == MMAL_ENOSPC)
  {
    int num_encodings = (sup_encodings.header.size - sizeof(sup_encodings.header)) / sizeof(sup_encodings.encodings[0]);
    if(num_encodings > MAX_ENCODINGS_NUM)
      num_encodings = MAX_ENCODINGS_NUM;

    for(int i = 0; i < num_encodings; i++)
    {
      fprintf(stderr, "%.4s ", (char*)&sup_encodings.encodings[i]);
    }

    fprintf(stderr, "\n");
  }
}

void RPiCamera::start(int32_t width, int32_t height, int32_t framerate, MMAL_FOURCC_T encoding)
{
  MMAL_STATUS_T status;

  camResolution_.width = width;
  camResolution_.height = height;
  framerate_ = framerate;
  encoding_ = encoding;

  if(!pFrameQueue)
    pFrameQueue = mmal_queue_create();

  try
  {
    // fill the pCameraComponent
    setupCamera();

    setDefaultParameters();

    // configure unused still port
    setupStillPort();

    // configure unused video port
    setupVideoPort();

    // configure super-important preview port - the only one we really use
    setupPreviewPort();

    // Allocate pool for camera images
    fprintf(stderr, "Creating buffer pool for camera preview output port: %d x %dB\n", pPreviewPort->buffer_num, pPreviewPort->buffer_size);

    // Pool + queue to hold preview frames
    pPreviewPool = mmal_port_pool_create(pPreviewPort, pPreviewPort->buffer_num, pPreviewPort->buffer_size);
    if (!pPreviewPool)
    {
      fprintf(stderr, "Error allocating pool\n");
      status = MMAL_ENOMEM;
      throw mmal_error(status);
    }

    // Enable preview port callback
    status = mmal_port_enable(pPreviewPort, previewOutputCallback);
    if (status != MMAL_SUCCESS)
    {
      fprintf(stderr, "Failed to enable camera preview port\n");
      throw mmal_error(status);
    }

    // enable camera component
    status = mmal_component_enable(pCameraComponent);
    if (status != MMAL_SUCCESS)
    {
      fprintf(stderr, "camera component couldn't be enabled\n");
      throw mmal_error(status);
    }

    // fill preview port with buffers
    for(uint32_t i = 0; i < pPreviewPort->buffer_num; i++)
    {
      MMAL_BUFFER_HEADER_T* pBuf = mmal_queue_get(pPreviewPool->queue);
      if(!pBuf)
        fprintf(stderr, "Unable to get a required buffer %d from pool queue\n", i);

      status = mmal_port_send_buffer(pPreviewPort, pBuf);
      if(status != MMAL_SUCCESS)
      {
        fprintf(stderr, "Error sending buffer to port %d\n", status);
        throw mmal_error(status);
      }
    }
  }
  catch(const mmal_error& err)
  {
    if (pCameraComponent)
      mmal_component_destroy(pCameraComponent);

    throw;
  }
}

void RPiCamera::stop()
{
  if(pPreviewPool)
  {
    mmal_pool_destroy(pPreviewPool);
    pPreviewPool = 0;
  }

  // Disable all our ports that are not handled by connections
  if (pCameraComponent)
  {
    MMAL_PORT_T* pCameraVideoPort = pCameraComponent->output[MMAL_CAMERA_VIDEO_PORT];
    if (pCameraVideoPort && pCameraVideoPort->is_enabled)
      mmal_port_disable(pCameraVideoPort);

    mmal_component_disable(pCameraComponent);
    mmal_component_destroy(pCameraComponent);
    pCameraComponent = 0;
  }
}

// -100 - 100
void RPiCamera::setSaturation(int saturation)
{
  if(!pCameraComponent || saturation < -100 || saturation > 100)
    return;

  MMAL_RATIONAL_T value = {saturation, 100};
  mmal_port_parameter_set_rational(pCameraComponent->control, MMAL_PARAMETER_SATURATION, value);
}

// -100 - 100
void RPiCamera::setSharpness(int sharpness)
{
  if(!pCameraComponent || sharpness < -100 || sharpness > 100)
    return;

  MMAL_RATIONAL_T value = {sharpness, 100};
  mmal_port_parameter_set_rational(pCameraComponent->control, MMAL_PARAMETER_SHARPNESS, value);
}

// -100 - 100
void RPiCamera::setContrast(int contrast)
{
  if(!pCameraComponent || contrast < -100 || contrast > 100)
    return;

  MMAL_RATIONAL_T value = {contrast, 100};
  mmal_port_parameter_set_rational(pCameraComponent->control, MMAL_PARAMETER_CONTRAST, value);
}

// 0 - 100
void RPiCamera::setBrightness(int brightness)
{
  if(!pCameraComponent || brightness < 0 || brightness > 100)
    return;

  MMAL_RATIONAL_T value = {brightness, 100};
  mmal_port_parameter_set_rational(pCameraComponent->control, MMAL_PARAMETER_BRIGHTNESS, value);
}

// 100, 200, 400, 800
void RPiCamera::setIso(CameraIso iso)
{
  if(!pCameraComponent)
    return;

  uint32_t val;

  switch(iso)
  {
    case CAMERA_ISO_200: val = 200; break;
    case CAMERA_ISO_400: val = 400; break;
    case CAMERA_ISO_800: val = 800; break;
    default: val = 100; break;
  }

  mmal_port_parameter_set_uint32(pCameraComponent->control, MMAL_PARAMETER_ISO, val);
}

void RPiCamera::setMeteringMode(MMAL_PARAM_EXPOSUREMETERINGMODE_T mode)
{
  if(!pCameraComponent)
    return;

  MMAL_PARAMETER_EXPOSUREMETERINGMODE_T meter_mode = {{MMAL_PARAMETER_EXP_METERING_MODE, sizeof(meter_mode)}, mode};
  mmal_port_parameter_set(pCameraComponent->control, &meter_mode.hdr);
}

// -10 - 10
void RPiCamera::setExposureCompensation(int expComp)
{
  if(!pCameraComponent)
    return;

  mmal_port_parameter_set_int32(pCameraComponent->control, MMAL_PARAMETER_EXPOSURE_COMP , expComp);
}

void RPiCamera::setExposureMode(MMAL_PARAM_EXPOSUREMODE_T mode)
{
  if(!pCameraComponent)
    return;

  MMAL_PARAMETER_EXPOSUREMODE_T exp_mode = {{MMAL_PARAMETER_EXPOSURE_MODE,sizeof(exp_mode)}, mode};
  mmal_port_parameter_set(pCameraComponent->control, &exp_mode.hdr);
}

void RPiCamera::setAwbMode(MMAL_PARAM_AWBMODE_T awb_mode)
{
  if(!pCameraComponent)
    return;

  MMAL_PARAMETER_AWBMODE_T param = {{MMAL_PARAMETER_AWB_MODE,sizeof(param)}, awb_mode};
  mmal_port_parameter_set(pCameraComponent->control, &param.hdr);
}

void RPiCamera::setAwbGains(float r_gain, float b_gain)
{
  if(!pCameraComponent || r_gain < 0.0f || b_gain < 0.0f)
    return;

  MMAL_PARAMETER_AWB_GAINS_T param = {{MMAL_PARAMETER_CUSTOM_AWB_GAINS,sizeof(param)}, {0,0}, {0,0}};

  param.r_gain.num = (unsigned int) (r_gain * 65536);
  param.b_gain.num = (unsigned int) (b_gain * 65536);
  param.r_gain.den = param.b_gain.den = 65536;
  mmal_port_parameter_set(pCameraComponent->control, &param.hdr);
}

// 0, 90, 180, 270
void RPiCamera::setRotation(CameraRotation rotation)
{
  if(!pCameraComponent)
    return;

  int32_t val;
  switch(rotation)
  {
    case CAMERA_ROTATE_90: val = 90; break;
    case CAMERA_ROTATE_180: val = 180; break;
    case CAMERA_ROTATE_270: val = 270; break;
    default: val = 0; break;
  }

  mmal_port_parameter_set_int32(pCameraComponent->output[0], MMAL_PARAMETER_ROTATION, val);
  mmal_port_parameter_set_int32(pCameraComponent->output[1], MMAL_PARAMETER_ROTATION, val);
  mmal_port_parameter_set_int32(pCameraComponent->output[2], MMAL_PARAMETER_ROTATION, val);
}

// H, V, both
void RPiCamera::setMirror(CameraMirror flip)
{
  if(!pCameraComponent)
    return;

  MMAL_PARAMETER_MIRROR_T mirror = {{MMAL_PARAMETER_MIRROR, sizeof(MMAL_PARAMETER_MIRROR_T)}, MMAL_PARAM_MIRROR_NONE};

  switch(flip)
  {
    case CAMERA_MIRROR_HORIZONTAL: mirror.value = MMAL_PARAM_MIRROR_HORIZONTAL; break;
    case CAMERA_MIRROR_VERTICAL: mirror.value = MMAL_PARAM_MIRROR_VERTICAL; break;
    case CAMERA_MIRROR_BOTH: mirror.value = MMAL_PARAM_MIRROR_BOTH; break;
    default: mirror.value = MMAL_PARAM_MIRROR_NONE; break;
  }

  mmal_port_parameter_set(pCameraComponent->output[0], &mirror.hdr);
  mmal_port_parameter_set(pCameraComponent->output[1], &mirror.hdr);
  mmal_port_parameter_set(pCameraComponent->output[2], &mirror.hdr);
}

void RPiCamera::setShutterSpeed(int speed_us)
{
  if(!pCameraComponent)
    return;

  mmal_port_parameter_set_uint32(pCameraComponent->control, MMAL_PARAMETER_SHUTTER_SPEED, speed_us);
}

void RPiCamera::setAlgorithmControl(MMAL_PARAMETER_ALGORITHM_CONTROL_ALGORITHMS_T algo, bool enable)
{
  if(!pCameraComponent)
    return;

  MMAL_PARAMETER_ALGORITHM_CONTROL_T ctrlConfig =
  {
    .hdr = { MMAL_PARAMETER_ALGORITHM_CONTROL, sizeof(ctrlConfig) },
    .algorithm = algo,
    .enabled = enable ? 1 : 0,
  };

  mmal_port_parameter_set(pCameraComponent->control, &ctrlConfig.hdr);
}

void RPiCamera::setUseCase(MMAL_PARAM_CAMERA_USE_CASE_T useCase)
{
  if(!pCameraComponent)
    return;

  MMAL_PARAMETER_CAMERA_USE_CASE_T useCaseCfg = {{ MMAL_PARAMETER_CAMERA_USE_CASE, sizeof(useCaseCfg) }, useCase};
  mmal_port_parameter_set(pCameraComponent->control, &useCaseCfg.hdr);
}

void RPiCamera::setZeroShutterLag(bool enable)
{
  if(!pCameraComponent)
    return;

  MMAL_PARAMETER_ZEROSHUTTERLAG_T zeroLagCfg =
  {
    .hdr = { MMAL_PARAMETER_ZERO_SHUTTER_LAG, sizeof(zeroLagCfg) },
    .zero_shutter_lag_mode = enable ? 1 : 0,
    .concurrent_capture = 0,
  };

  mmal_port_parameter_set(pCameraComponent->control, &zeroLagCfg.hdr);
}

void RPiCamera::setAnalogGain(float analog)
{
  if(!pCameraComponent)
    return;

  MMAL_RATIONAL_T rational = { 0, 65536 };
  rational.num = (unsigned int) (analog * 65536);
  mmal_port_parameter_set_rational(pCameraComponent->control, MMAL_PARAMETER_ANALOG_GAIN, rational);
}

void RPiCamera::setDigitalGain(float digital)
{
  if(!pCameraComponent)
    return;

  MMAL_RATIONAL_T rational = { 0, 65536 };
  rational.num = (unsigned int) (digital * 65536);
  mmal_port_parameter_set_rational(pCameraComponent->control, MMAL_PARAMETER_DIGITAL_GAIN, rational);
}

}

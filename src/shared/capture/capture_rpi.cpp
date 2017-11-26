//========================================================================
//  This software is free: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License Version 3,
//  as published by the Free Software Foundation.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  Version 3 in the file COPYING that came with this distribution.
//  If not, see <http://www.gnu.org/licenses/>.
//========================================================================
/*!
  \file    capture_rpi.cpp
  \brief   C++ Implementation: CaptureRPi
  \author  Andre Ryll, (C) 2017
*/
//========================================================================

#include "capture_rpi.h"
#include "interface/mmal/mmal_encodings.h"

#ifndef VDATA_NO_QT
CaptureRPi::CaptureRPi(VarList * _settings,int default_camera_id, QObject * parent) : QObject(parent), CaptureInterface(_settings)
#else
CaptureRPi::CaptureRPi(VarList * _settings,int default_camera_id) : CaptureInterface(_settings)
#endif
{
  is_capturing = false;

  #ifndef VDATA_NO_QT
    mutex.lock();
  #endif

  settings->addChild(capture_settings = new VarList("Capture Settings"));
  settings->addChild(dcam_parameters  = new VarList("Camera Parameters"));

  //=======================CAPTURE SETTINGS==========================
  capture_settings->addChild(v_max_fps = new VarInt("Max FPS", 60, 1, 90));
  capture_settings->addChild(v_resolution = new VarStringEnum("Resolution", resolutionToString(RES_640X480)));
  v_resolution->addItem(resolutionToString(RES_640X480));
  v_resolution->addItem(resolutionToString(RES_1280X960));
  capture_settings->addChild(v_colorout = new VarStringEnum("color mode", Colors::colorFormatToString(COLOR_RGB8)));
  v_colorout->addItem(Colors::colorFormatToString(COLOR_RGB8));
  v_colorout->addItem(Colors::colorFormatToString(COLOR_YUV422_UYVY));

  //=======================DCAM PARAMETERS===========================
  dcam_parameters->addFlags( VARTYPE_FLAG_HIDE_CHILDREN );

  v_expose_us = new VarInt("Expose [us]", 5000, 10, 100000);
  v_analog_gain = new VarDouble("Analog Gain", 2.0, 0.0, 20.0);
  v_digital_gain = new VarDouble("Digital Gain", 2.0, 0.0, 20.0);

  v_mirror_top_down = new VarBool("Mirror Top/Down");
  v_mirror_left_right = new VarBool("Mirror Left/Right");
  v_wb_red = new VarDouble("WB Red", 1.0, 0.1, 10.0);
  v_wb_blue = new VarDouble("WB Blue", 1.0, 0.1, 10.0);
  v_sharpness = new VarInt("Sharpness", 0, -100, 100);
  v_contrast = new VarInt("Contrast", 0, -100, 100);
  v_brightness = new VarInt("Brightness", 50, 0, 100);
  v_saturation = new VarInt("Saturation", 0, -100, 100);
  
  dcam_parameters->addChild(v_expose_us);
  dcam_parameters->addChild(v_analog_gain);
  dcam_parameters->addChild(v_digital_gain);
  dcam_parameters->addChild(v_mirror_top_down);
  dcam_parameters->addChild(v_mirror_left_right);
  dcam_parameters->addChild(v_wb_red);
  dcam_parameters->addChild(v_wb_blue);
  dcam_parameters->addChild(v_sharpness);
  dcam_parameters->addChild(v_contrast);
  dcam_parameters->addChild(v_brightness);
  dcam_parameters->addChild(v_saturation);
  
  #ifndef VDATA_NO_QT
    mvc_connect(dcam_parameters);
    mutex.unlock();
  #endif
}

#ifndef VDATA_NO_QT
void CaptureRPi::mvc_connect(VarList * group)
{
  vector<VarType *> v=group->getChildren();
  for (unsigned int i=0;i<v.size();i++)
  {
    connect(v[i],SIGNAL(wasEdited(VarType *)),group,SLOT(mvcEditCompleted()));
  }
  connect(group,SIGNAL(wasEdited(VarType *)),this,SLOT(changed(VarType *)));
}

void CaptureRPi::changed(VarType * group)
{
  if (group->getType()==VARTYPE_ID_LIST)
  {
    writeParameterValues( (VarList *)group );
    readParameterValues( (VarList *)group );
  }
}
#endif

void CaptureRPi::readAllParameterValues()
{
  readParameterValues(dcam_parameters);
}

void CaptureRPi::writeAllParameterValues()
{
  writeParameterValues(dcam_parameters);
}

void CaptureRPi::readParameterValues(VarList * item)
{
}

void CaptureRPi::writeParameterValues(VarList * item)
{
  if(item != dcam_parameters)
    return;

  #ifndef VDATA_NO_QT
    mutex.lock();
  #endif

  camera.setShutterSpeed(v_expose_us->getInt());

  camera.setAnalogGain(v_analog_gain->getDouble());
  camera.setDigitalGain(v_digital_gain->getDouble());

  rpi::CameraMirror mir = rpi::CAMERA_MIRROR_NONE;
  if(v_mirror_top_down->getBool() && v_mirror_left_right->getBool())
    mir = rpi::CAMERA_MIRROR_BOTH;
  else if(v_mirror_top_down->getBool())
    mir = rpi::CAMERA_MIRROR_VERTICAL;
  else if(v_mirror_left_right->getBool())
    mir = rpi::CAMERA_MIRROR_HORIZONTAL;

  camera.setMirror(mir);

  camera.setAwbGains(v_wb_red->getDouble(), v_wb_blue->getDouble());

  camera.setSharpness(v_sharpness->getInt());
  camera.setContrast(v_contrast->getInt());
  camera.setBrightness(v_brightness->getInt());
  camera.setSaturation(v_saturation->getInt());

  #ifndef VDATA_NO_QT
    mutex.unlock();
  #endif
}

CaptureRPi::~CaptureRPi()
{
  capture_settings->deleteAllChildren();
  dcam_parameters->deleteAllChildren();
}

bool CaptureRPi::resetBus()
{
  #ifndef VDATA_NO_QT
    mutex.lock();
  #endif

  #ifndef VDATA_NO_QT
    mutex.unlock();
  #endif
    
  return true;
}

bool CaptureRPi::stopCapture()
{
  if (isCapturing())
  {
    readAllParameterValues();

    camera.stop();

    pLastFrame = 0;
    
    is_capturing = false;
  }
  
  vector<VarType *> tmp = capture_settings->getChildren();
  for (unsigned int i=0; i < tmp.size();i++)
  {
    tmp[i]->removeFlags( VARTYPE_FLAG_READONLY );
  }
  
  dcam_parameters->addFlags( VARTYPE_FLAG_HIDE_CHILDREN );
  
  return true;
}

bool CaptureRPi::startCapture()
{
  #ifndef VDATA_NO_QT
    mutex.lock();
  #endif
   
  MMAL_FOURCC_T encoding = MMAL_ENCODING_RGB24;
  ColorFormat out_color = Colors::stringToColorFormat(v_colorout->getSelection().c_str());
  if(out_color == COLOR_RGB8)
  {
  }
  else
  {
    encoding = MMAL_ENCODING_UYVY;
  }

  int maxFps = v_max_fps->getInt();

  width_ = 640;
  height_ = 480;
  if(stringToResolution(v_resolution->getSelection().c_str()) == RES_1280X960)
  {
    width_ = 1280;
    height_ = 960;
  }

  try
  {
    camera.start(width_, height_, maxFps, encoding);
  }
  catch(exception& e)
  {
    fprintf(stderr, "RPi: failed to start camera %s\n", e.what());
    #ifndef VDATA_NO_QT
      mutex.unlock();
    #endif
    return false;
  }
  
  is_capturing = true;
  
  vector<VarType *> tmp = capture_settings->getChildren();
  for (unsigned int i=0; i < tmp.size();i++) {
    tmp[i]->addFlags( VARTYPE_FLAG_READONLY );
  }
    
  dcam_parameters->removeFlags( VARTYPE_FLAG_HIDE_CHILDREN );

  #ifndef VDATA_NO_QT
    mutex.unlock();
  #endif
    
  printf("RPi Info: Restoring Previously Saved Camera Parameters\n");
  writeAllParameterValues();
  readAllParameterValues();
  
  return true;
}

bool CaptureRPi::copyAndConvertFrame(const RawImage & src, RawImage & target)
{
  if(!src.getData())
    return false;

  #ifndef VDATA_NO_QT
    mutex.lock();
  #endif

  ColorFormat src_fmt = src.getColorFormat();
  
  if(target.getData() == 0)
  {
    //allocate target, if it does not exist yet
    target.allocate(src_fmt, src.getWidth(), src.getHeight());
  } 
  else
  {
    target.ensure_allocation(src_fmt, src.getWidth(), src.getHeight());
  }
  target.setTime(src.getTime());
  
  if(src.getColorFormat() == COLOR_RGB8)
  {
    memcpy(target.getData(),src.getData(),src.getNumBytes());
  }
  else
  {
    for(int i = 0; i < src.getNumBytes(); i += 2)
    {
      target.getData()[i+1] = src.getData()[i];
      target.getData()[i] = src.getData()[i+1];
    }
  }
  
  #ifndef VDATA_NO_QT
    mutex.unlock();
  #endif

  return true;
}

RawImage CaptureRPi::getFrame()
{
  #ifndef VDATA_NO_QT
    mutex.lock();
  #endif
    
  RawImage result;
//  result.setColorFormat(capture_format);
  ColorFormat out_color = Colors::stringToColorFormat(v_colorout->getSelection().c_str());
  result.setColorFormat(out_color);
  result.setWidth(0);
  result.setHeight(0);
  result.setTime(0.0);
  result.setData(0);
  
  MMAL_BUFFER_HEADER_T* pBuf = camera.waitForFrame(200);

  if(pBuf)
  {
    mmal_buffer_header_mem_lock(pBuf);

    timeval tv;
    gettimeofday(&tv,NULL);
    result.setTime((double)tv.tv_sec + tv.tv_usec*(1.0E-6));
    result.setWidth(width_);
    result.setHeight(height_);
//    ColorFormat out_color = Colors::stringToColorFormat(v_colorout->getSelection().c_str());
//    result.setColorFormat(out_color);
    result.setData((unsigned char*)pBuf->data);
//    fprintf(stderr, "Retrieved camera frame, lengt: %u\n", pBuf->length);
//     fprintf(stderr, "Timestamp_us: %ld\n", pRequest->infoTimeStamp_us.read());
//     fprintf(stderr, "BPP: %u\n", pRequest->imageBytesPerPixel.read());
//     fprintf(stderr, "PixelFormat: %s\n", pRequest->imagePixelFormat.readS().c_str());
  }
  else
  {
    fprintf(stderr, "RPi: request not OK\n");
  }
  
  pLastFrame = pBuf;
  
  #ifndef VDATA_NO_QT
    mutex.unlock();
  #endif
  return result;
}

void CaptureRPi::releaseFrame() 
{
  #ifndef VDATA_NO_QT
    mutex.lock();
  #endif

  if(pLastFrame)
  {
    mmal_buffer_header_mem_unlock(pLastFrame);
    camera.releaseFrame(pLastFrame);
    pLastFrame = 0;
  }
  
  #ifndef VDATA_NO_QT
    mutex.unlock();
  #endif
}

string CaptureRPi::getCaptureMethodName() const 
{
  return "RPi Camera";
}

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
  \file    capture_rpi.h
  \brief   C++ Interface: CaptureRPi
  \author  Andre Ryll, (C) 2017
*/
//========================================================================

#ifndef CAPTURE_RPI_H
#define CAPTURE_RPI_H
#include "captureinterface.h"
#include "RPiCamera.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "VarTypes.h"

#ifndef VDATA_NO_QT
  #include <QMutex>
#else
  #include <pthread.h>
#endif

/*!
  \class CaptureRPi
  \brief A capture class for Raspberry Pi cameras
  \author  Andre Ryll, (C) 2017

  This class provides the ability to use and configure Raspberry Pi
  cameras using the low-level MMAL interface driver.

  Overall, it not only provides capture-abilities, but also full
  on-the-fly configuration abilities through the VarTypes system.
 
  If you find your camera not working correctly, or discover a bug,
  please inform the author, as we are aiming for complete camera
  coverage.
*/
#ifndef VDATA_NO_QT
  #include <QMutex>
  //if using QT, inherit QObject as a base
class CaptureRPi : public QObject, public CaptureInterface
#else
class CaptureRPi : public CaptureInterface
#endif
{
#ifndef VDATA_NO_QT
  Q_OBJECT
  
  public slots:
  void changed(VarType * group);
  
  protected:
  QMutex mutex;
  
  public:
#endif

  enum Resolution
  {
    RES_640X480,
    RES_1280X960,
  };

  static Resolution stringToResolution(const char* s)
  {
    if(strcmp(s,"640x480")==0) {
      return RES_640X480;
    } else if (strcmp(s,"1280x960")==0) {
      return RES_1280X960;
    } else {
      return RES_640X480;
    }
  }

  static string resolutionToString(Resolution res)
  {
    if(res == RES_1280X960)
      return "1280x960";
    else
      return "640x480";
  }

protected:
  bool is_capturing;

  //CAM parameters:
  VarInt* v_expose_us;
  VarDouble* v_analog_gain;
  VarDouble* v_digital_gain;
  VarBool* v_mirror_top_down;
  VarBool* v_mirror_left_right;
  VarDouble* v_wb_red;
  VarDouble* v_wb_blue;
  VarInt* v_sharpness;
  VarInt* v_contrast;
  VarInt* v_brightness;
  VarInt* v_saturation;
  
  //capture variables:
  VarInt* v_max_fps;
  VarStringEnum* v_resolution;
  VarStringEnum* v_colorout;
  
  VarList* dcam_parameters;
  VarList* capture_settings;
  
  // RPi specific data
  rpi::RPiCamera camera;

  MMAL_BUFFER_HEADER_T* pLastFrame;

  int width_;
  int height_;

//  unsigned int cam_id;
//  ColorFormat capture_format;

public:
  #ifndef VDATA_NO_QT
    CaptureRPi(VarList * _settings=0,int default_camera_id=0,QObject * parent=0);
    void mvc_connect(VarList * group);
  #else
    CaptureRPi(VarList * _settings=0,int default_camera_id=0);
  #endif
  ~CaptureRPi();

  /// Initialize the interface and start capture
  virtual bool startCapture();

  /// Stop Capture
  virtual bool stopCapture();

  virtual bool isCapturing() { return is_capturing; };

  /// this gives a raw-image with a pointer directly to the video-buffer
  /// Note that this pointer is only guaranteed to point to a valid
  /// memory location until releaseFrame() is called.
  virtual RawImage getFrame();

  virtual void releaseFrame();

  virtual bool resetBus();

  void readParameterValues(VarList * item);

  void writeParameterValues(VarList * item);

  virtual void readAllParameterValues();

  void writeAllParameterValues();

  virtual bool copyAndConvertFrame(const RawImage & src, RawImage & target);

  virtual string getCaptureMethodName() const;
};

#endif

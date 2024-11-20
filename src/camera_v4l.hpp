/* Code adapted from: https://github.com/jacksonliam/mjpg-streamer/tree/master
#                                                                              #
# This package work with the Logitech UVC based webcams with the mjpeg feature #
#                                                                              #
# Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard                   #
#                    2007 Lucas van Staden                                     #
#                    2007 Tom Stöveken                                         #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/


#include "camera.hpp"
#include "rjpg-capture.hpp"

extern "C" {
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>  
  #include <stdlib.h>
  #include <sys/mman.h>
  #include <sys/ioctl.h>  
  #include <string.h>  
  #include <linux/types.h>          /* for videodev2.h */
  #include <linux/videodev2.h>
}

struct Camera_V4L : public Camera
{
  struct ErrorIOCTL : public std::runtime_error
  { using std::runtime_error::runtime_error; };
  struct ErrorCapture : public std::runtime_error
  { using std::runtime_error::runtime_error; };

  struct CaptureBuffer 
  {
    void *start = nullptr;
    size_t length = 0;
  };

  int _fd = -1; 
  unsigned int _width = 0;
  unsigned int _height = 0;
  static constexpr int _bufferCount = 4;
  CaptureBuffer _captureBuffers[_bufferCount];

  /* ioctl with a number of retries in the case of failure
  * args:
  * fd - device descriptor
  * IOCTL_X - ioctl reference
  * arg - pointer to ioctl data
  * returns - ioctl result
  */
  int xioctl(int fd, unsigned long IOCTL_X, void *arg)
  {
      int ret = 0;
      int tries = 4;

      do {
          ret = ioctl(fd, IOCTL_X, arg);
      } while(ret && tries-- &&
              ((errno == EINTR) || (errno == EAGAIN) || (errno == ETIMEDOUT)));

      if(ret && (tries <= 0)) fprintf(stderr, "ioctl (%i) retried - giving up: %s)\n", IOCTL_X, strerror(errno));

      return (ret);
  }

  template <typename S> void zero_struct(S &data)
  {
    memset(&data, 0, sizeof(data));
  }

  template <typename S> void ioctl_get(unsigned long ioctl_code, S &data, const std::string &what)
  {
    if (xioctl(_fd, ioctl_code, &data) != 0) 
    {
      LogError("ioctl_get failed: %s", what.c_str());
      throw ErrorIOCTL(string_format("ioctl_get failed for %s", what.c_str()));
    }
  }

  template <typename S> void ioctl_set(unsigned long ioctl_code, S &data, const std::string &what)
  {
    if (xioctl(_fd, ioctl_code, (void*)&data) != 0) 
    {
      LogError("ioctl_set failed: %s", what.c_str());
      throw ErrorIOCTL(string_format("ioctl_set failed for %s", what.c_str()));
    }
  }

  // some ioctl are both getting and setting
  template <typename S> void ioctl_rw(unsigned long ioctl_code, S &data, const std::string &what)
  {
    if (xioctl(_fd, ioctl_code, &data) != 0) 
    {
      LogError("ioctl_rw failed: %s", what.c_str());
      throw ErrorIOCTL(string_format("ioctl_rw failed for %s", what.c_str()));
    }
  }


  virtual void open(const std::string &path) override 
  {
    if (_fd != -1)
    {
      ::close(_fd);
    }

    _fd = ::open(path.c_str(), O_RDWR);

    if (_fd == -1)
    {
      throw ErrorOpen("could not open camera device");
    }

    struct v4l2_capability cap;

    ioctl_get(VIDIOC_QUERYCAP, cap, "query capabilities");

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
    {
      LogDeb("%s has capture capability", path.c_str());
    }
    else
    {
      LogError("%s does not have capture capability", path.c_str());
      throw ErrorOpen("no capture capability");
    }


    if (cap.capabilities & V4L2_CAP_STREAMING)
    {
      LogDeb("%s has streaming capability", path.c_str());
    }
    else
    {
      LogError("%s does not have streaming capability", path.c_str());
      throw ErrorOpen("no streaming capability");
    }

    LogDeb("%s name is %s", path.c_str(), cap.driver);
    LogDeb("%s card is %s", path.c_str(), cap.card);

    struct v4l2_dv_timings timings = {0};

/*
    try {
    ioctl_get(VIDIOC_QUERY_DV_TIMINGS, timings, "query video timings");
    }
    catch(std::runtime_error e) {
      LogError("Could not query video timings (%d), but continuing...", errno);
    }

    try {
    // not sure why it sets them right after getting them
    ioctl_set(VIDIOC_QUERY_DV_TIMINGS, timings, "set video timings");
    }
    catch(std::runtime_error e) {
      LogError("Could not set video timings (%d), but continuing...", errno);
    }
*/
    
    LogDeb("Got timing size %ux%u pixclk %llu\n", timings.bt.width, timings.bt.height, timings.bt.pixelclock);

    _width = timings.bt.width;
    _height = timings.bt.height;

    if (_width == 0)
    {
      _width = 1080;
      LogError("Have width 0, will default to %d", _width);
    }
    if (_height == 0)
    {
      _height = 720;
      LogError("Have height 0, will default to %d", _height);
    }

    struct v4l2_event_subscription sub = {0};

    sub.type = V4L2_EVENT_SOURCE_CHANGE;
    try {
    ioctl_rw(VIDIOC_SUBSCRIBE_EVENT, sub, "subscribe to change events");
    }
    catch(std::runtime_error e) {
      LogError("Could not subscribe to source change event (%d), but continuing...", errno);
    }

    struct v4l2_format format = {0};

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = _width;
    format.fmt.pix.height = _height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    // format.fmt.pix.field = V4L2_FIELD_ANY;

    ioctl_rw(VIDIOC_S_FMT, format, "set video format");


    if (format.fmt.pix.width != _width || format.fmt.pix.height != _height)
    {
      _width = format.fmt.pix.width;
      _height = format.fmt.pix.height;
      LogDeb("Adjusting image size to %d x %d", _width, _height);
    }


    struct v4l2_streamparm fps_config = {0};

    fps_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ioctl_rw(VIDIOC_G_PARM, fps_config, "get FPS settings");

    LogDeb("FPS timing %u/%u", fps_config.parm.capture.timeperframe.numerator, fps_config.parm.capture.timeperframe.denominator);

    v4l2_requestbuffers reqbuf_config;

    reqbuf_config.count = _bufferCount;
    reqbuf_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf_config.memory = V4L2_MEMORY_MMAP;

    ioctl_set(VIDIOC_REQBUFS, reqbuf_config, "setup video buffers");

    for(int i=0; i < _bufferCount; i++)
    {
      v4l2_buffer buffer_config;

      buffer_config.index = i;
      buffer_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer_config.memory = V4L2_MEMORY_MMAP;

      ioctl_get(VIDIOC_QUERYBUF, buffer_config, "getting buffer config");

      _captureBuffers[i].start = 
        mmap(0 /* start anywhere */ ,
          buffer_config.length, 
          PROT_READ | PROT_WRITE, 
          MAP_SHARED, _fd,
          buffer_config.m.offset);

      if (_captureBuffers[i].start == MAP_FAILED)
      {
        LogError("mmap of capture buffer of size %d failed.", (int)buffer_config.length);
        throw ErrorOpen("mmap of capture buffer failed");
      }

      _captureBuffers[i].length = buffer_config.length;
    }

    for(int i=0; i < _bufferCount; i++)
    {
      v4l2_buffer buffer_config;

      buffer_config.index = i;
      buffer_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer_config.memory = V4L2_MEMORY_MMAP;

      ioctl_set(VIDIOC_QBUF, buffer_config, "queue buffer");
    }

    enable_streaming(true);
  }

  void enable_streaming(bool enable_it = true)
  {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (enable_it)
    {
      ioctl_set(VIDIOC_STREAMON, type, "enable streaming");
    }
    else
    {
      ioctl_set(VIDIOC_STREAMOFF, type, "disable streaming");
    }
  }

// for detecting bogus JPEG frames
#define HEADERFRAME1 0xaf

  virtual void read_image_bytes(std::vector<char> &data) override
  {
    // enable_streaming(true);

    struct v4l2_buffer buffer_config;

    zero_struct(buffer_config);

    buffer_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_config.memory = V4L2_MEMORY_MMAP;

    LogDeb("read_image_bytes: dequeue frame from buffer for fd %d...", _fd);

    ioctl_rw(VIDIOC_DQBUF, buffer_config, "dequeue buffer");

    LogDeb("read_image_bytes: got frame of size %d from buffer %d", buffer_config.bytesused, buffer_config.index);

    if (buffer_config.bytesused <= HEADERFRAME1)
    {
      LogDeb("ignoring empty-ish buffer of size %d", (int)buffer_config.bytesused);
      data.clear();
      return;
    }

    if (buffer_config.index >= _bufferCount)
    {
      LogError("invalid buffer index %d", buffer_config.index);
      throw ErrorCapture("invalid buffer index");
    }

    data.resize(buffer_config.bytesused);
    memcpy(&data.front(), _captureBuffers[buffer_config.index].start, buffer_config.bytesused);
    
    ioctl_set(VIDIOC_QBUF, buffer_config, "requeue buffer");

    // enable_streaming(false);
  }

  virtual void close() override
  {
    if (_fd != -1)
    {
      try
      {
        enable_streaming(false);
      }
      catch(const std::exception& e)
      {
        LogError("error closing down stream fd=%d", _fd);
      }
      for(int i=0; i<_bufferCount; i++)
      {
        if (_captureBuffers[i].start != nullptr)
        {
          munmap(_captureBuffers[i].start, _captureBuffers[i].length);
          _captureBuffers[i].start = nullptr;
          _captureBuffers[i].length = 0;
        }
      }
      ::close(_fd);
      _fd = -1;
    }
  }


};

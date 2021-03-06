/******************************************************************************
 * Copyright © 2012-2014 Institut für Nachrichtentechnik, Universität Rostock *
 * Copyright © 2006-2012 Quality & Usability Lab,                             *
 *                       Telekom Innovation Laboratories, TU Berlin           *
 *                                                                            *
 * This file is part of the SoundScape Renderer (SSR).                        *
 *                                                                            *
 * The SSR is free software:  you can redistribute it and/or modify it  under *
 * the terms of the  GNU  General  Public  License  as published by the  Free *
 * Software Foundation, either version 3 of the License,  or (at your option) *
 * any later version.                                                         *
 *                                                                            *
 * The SSR is distributed in the hope that it will be useful, but WITHOUT ANY *
 * WARRANTY;  without even the implied warranty of MERCHANTABILITY or FITNESS *
 * FOR A PARTICULAR PURPOSE.                                                  *
 * See the GNU General Public License for more details.                       *
 *                                                                            *
 * You should  have received a copy  of the GNU General Public License  along *
 * with this program.  If not, see <http://www.gnu.org/licenses/>.            *
 *                                                                            *
 * The SSR is a tool  for  real-time  spatial audio reproduction  providing a *
 * variety of rendering algorithms.                                           *
 *                                                                            *
 * http://spatialaudio.net/ssr                           ssr@spatialaudio.net *
 ******************************************************************************/

/// @file
/// InterSense tracker (implementation).

#ifdef HAVE_CONFIG_H
#include <config.h>  // for ENABLE_*, HAVE_*, WITH_*
#endif

#include <thread>   // std::this_thread::sleep_for
#include <chrono>   // std::chrono::milliseconds
#include <fstream>

#include "trackerintersense.h"
#include "api.h"  // for Publisher
#include "legacy_orientation.h"  // for Orientation
#include "ssr_global.h"
#include "posixpathtools.h"

ssr::TrackerInterSense::TrackerInterSense(api::Publisher& controller
    , const std::string& ports, const unsigned int read_interval)
  : Tracker()
  , _controller(controller)
  , _read_interval(read_interval)
  , _stop_thread(false)
{
  // suppress output of libisense.so
  //int stdout_fileno = fileno(stdout);
  //int stdout_handle = dup(stdout_fileno);
  //int stderr_fileno = fileno(stderr);
  //int stderr_handle = dup(stderr_fileno);

  VERBOSE("Looking for InterSense tracker.");

  //close outputs of libisense.so
  //close(stdout_fileno);
  //close(stderr_fileno);

  // save current working directory
  std::string current_path;
  posixpathtools::getcwd(current_path);

  // if specific serial ports were given: use them
  if (ports != "")
  {
    // switch working directory
    chdir("/tmp");
    VERBOSE("Creating /tmp/isports.ini to configure InterSense tracker ports.");

    // create isports.ini
    std::ofstream file;
    file.open("isports.ini", std::ios::trunc);
    if (file.is_open())
    {
      std::istringstream ports_stream(ports);
      std::string port;
      int i = 1;
      while (ports_stream >> port)
      {
        file << "Port" << i++ << " = " << port << std::endl;
      }
      file.close();
    }
    else
    {
      ERROR("Could not create /tmp/isports.ini to configure InterSense tracker ports");
    }
  }
  else
  {
    VERBOSE("Letting InterSense tracker look for isports.ini in current working directory.");
  }

  // start tracker (will automatically try all listed ports in isports.ini file in
  // current working directory)
  _tracker_h = ISD_OpenTracker(static_cast<Hwnd>(0), 0, FALSE, FALSE);

  if (ports != "")
  {
    // restore working directory
    chdir(current_path.c_str());
  }

  // restore stdout and stderr
  //dup2(stdout_handle, stdout_fileno);
  //dup2(stderr_handle, stderr_fileno);

  // no tracker found
  if (_tracker_h <= 0)
  {
    throw std::runtime_error("InterSense tracker not found!");
  }

  // if tracker is there
  else
  {
    VERBOSE("InterSense tracker found.");

    _start();

    // wait 100ms to make sure that tracker gives reliable values
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // and then calibrate it
    calibrate();
  }
}

ssr::TrackerInterSense::~TrackerInterSense()
{
  // stop thread
  _stop();
  ISD_CloseTracker(_tracker_h);
}

ssr::TrackerInterSense::ptr_t
ssr::TrackerInterSense::create(api::Publisher& controller
    , const std::string& ports, const unsigned int read_interval)
{
  ptr_t temp; // temp = NULL
  try
  {
    temp.reset(new TrackerInterSense(controller, ports, read_interval));
  }
  catch(std::runtime_error& e)
  {
    ERROR(e.what());
  }
  return temp;
}

void ssr::TrackerInterSense::calibrate()
{
  ISD_ResetHeading(_tracker_h, 1);
}

void
ssr::TrackerInterSense::_start()
{
  // create thread
  _tracker_thread = std::thread(&ssr::TrackerInterSense::_thread, this);
  VERBOSE("Starting tracker ...");
}

void
ssr::TrackerInterSense::_stop()
{
  _stop_thread = true;
  if (_tracker_thread.joinable())
  {
    VERBOSE2("Stopping tracker...");
    _tracker_thread.join();
  }
}

void
ssr::TrackerInterSense::_thread()
{
#ifdef HAVE_INTERSENSE_404
  ISD_TRACKING_DATA_TYPE tracker_data;
#else
  ISD_TRACKER_DATA_TYPE tracker_data;
#endif

  while(!_stop_thread)
  {
#ifdef HAVE_INTERSENSE_404
    ISD_GetTrackingData(_tracker_h, &tracker_data);
    _controller.take_control()->reference_rotation_offset(
        Orientation(-tracker_data.Station[0].Euler[0]
           + 90.0f));
#else
    ISD_GetData(_tracker_h, &tracker_data);
    _controller.take_control()->reference_rotation_offset(
        Orientation(-static_cast<float>(tracker_data.Station[0].Orientation[0])
           + 90.0f));
#endif

    // wait a bit
    std::this_thread::sleep_for(std::chrono::microseconds(_read_interval*1000u));
  };

}

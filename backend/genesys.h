/* sane - Scanner Access Now Easy.

   Copyright (C) 2003, 2004 Henning Meier-Geinitz <henning@meier-geinitz.de>
   Copyright (C) 2005-2013 Stephane Voltz <stef.dev@free.fr>
   Copyright (C) 2006 Laurent Charpentier <laurent_pubs@yahoo.com>
   Copyright (C) 2009 Pierre Willenbrock <pierre@pirsoft.dnsalias.org>

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.
*/

#ifndef GENESYS_H
#define GENESYS_H

#ifndef BACKEND_NAME
# define BACKEND_NAME genesys
#endif

#include "genesys_low.h"
#include <queue>

#ifndef PATH_MAX
# define PATH_MAX	1024
#endif

#if defined(_WIN32) || defined(HAVE_OS2_H)
# define PATH_SEP	'\\'
#else
# define PATH_SEP	'/'
#endif


#define ENABLE(OPTION)  s->opt[OPTION].cap &= ~SANE_CAP_INACTIVE
#define DISABLE(OPTION) s->opt[OPTION].cap |=  SANE_CAP_INACTIVE
#define IS_ACTIVE(OPTION) (((s->opt[OPTION].cap) & SANE_CAP_INACTIVE) == 0)

#define GENESYS_CONFIG_FILE "genesys.conf"

/* Maximum time for lamp warm-up */
#define WARMUP_TIME 65

#define STR_FLATBED "Flatbed"
#define STR_TRANSPARENCY_ADAPTER "Transparency Adapter"
#define STR_TRANSPARENCY_ADAPTER_INFRARED "Transparency Adapter Infrared"

#ifndef SANE_I18N
#define SANE_I18N(text) text
#endif

/** List of SANE options
 */
enum Genesys_Option
{
  OPT_NUM_OPTS = 0,

  OPT_MODE_GROUP,
  OPT_MODE,
  OPT_SOURCE,
  OPT_PREVIEW,
  OPT_BIT_DEPTH,
  OPT_RESOLUTION,

  OPT_GEOMETRY_GROUP,
  OPT_TL_X,			/* top-left x */
  OPT_TL_Y,			/* top-left y */
  OPT_BR_X,			/* bottom-right x */
  OPT_BR_Y,			/* bottom-right y */

  /* advanced image enhancement options */
  OPT_ENHANCEMENT_GROUP,
  OPT_CUSTOM_GAMMA,		/* toggle to enable custom gamma tables */
  OPT_GAMMA_VECTOR,
  OPT_GAMMA_VECTOR_R,
  OPT_GAMMA_VECTOR_G,
  OPT_GAMMA_VECTOR_B,
  OPT_SWDESKEW,
  OPT_SWCROP,
  OPT_SWDESPECK,
  OPT_DESPECK,
  OPT_SWSKIP,
  OPT_SWDEROTATE,
  OPT_BRIGHTNESS,
  OPT_CONTRAST,

  OPT_EXTRAS_GROUP,
  OPT_LAMP_OFF_TIME,
  OPT_LAMP_OFF,
  OPT_THRESHOLD,
  OPT_THRESHOLD_CURVE,
  OPT_DISABLE_DYNAMIC_LINEART,
  OPT_DISABLE_INTERPOLATION,
  OPT_COLOR_FILTER,
  OPT_CALIBRATION_FILE,
  OPT_EXPIRATION_TIME,

  OPT_SENSOR_GROUP,
  OPT_SCAN_SW,
  OPT_FILE_SW,
  OPT_EMAIL_SW,
  OPT_COPY_SW,
  OPT_PAGE_LOADED_SW,
  OPT_OCR_SW,
  OPT_POWER_SW,
  OPT_EXTRA_SW,
  OPT_NEED_CALIBRATION_SW,
  OPT_BUTTON_GROUP,
  OPT_CALIBRATE,
  OPT_CLEAR_CALIBRATION,
  OPT_FORCE_CALIBRATION,

  /* must come last: */
  NUM_OPTIONS
};

enum GenesysButtonName : unsigned {
    BUTTON_SCAN_SW = 0,
    BUTTON_FILE_SW,
    BUTTON_EMAIL_SW,
    BUTTON_COPY_SW,
    BUTTON_PAGE_LOADED_SW,
    BUTTON_OCR_SW,
    BUTTON_POWER_SW,
    BUTTON_EXTRA_SW,
    NUM_BUTTONS
};

GenesysButtonName genesys_option_to_button(int option);

class GenesysButton {
public:
    void write(bool value)
    {
        if (value == value_) {
            return;
        }
        values_to_read_.push(value);
        value_ = value;
    }

    bool read()
    {
        if (values_to_read_.empty()) {
            return value_;
        }
        bool ret = values_to_read_.front();
        values_to_read_.pop();
        return ret;
    }

private:
    bool value_ = false;
    std::queue<bool> values_to_read_;
};

/** Scanner object. Should have better be called Session than Scanner
 */
struct Genesys_Scanner
{
    Genesys_Scanner() = default;
    ~Genesys_Scanner() = default;

    // Next scanner in list
    struct Genesys_Scanner *next;

    // Low-level device object
    Genesys_Device* dev = nullptr;

    // SANE data
    // We are currently scanning
    SANE_Bool scanning;
    // Option descriptors
    SANE_Option_Descriptor opt[NUM_OPTIONS];

    // Option values
    SANE_Word bit_depth = 0;
    SANE_Word resolution = 0;
    bool preview = false;
    SANE_Word threshold = 0;
    SANE_Word threshold_curve = 0;
    bool disable_dynamic_lineart = false;
    bool disable_interpolation = false;
    bool lamp_off = false;
    SANE_Word lamp_off_time = 0;
    bool swdeskew = false;
    bool swcrop = false;
    bool swdespeck = false;
    bool swderotate = false;
    SANE_Word swskip = 0;
    SANE_Word despeck = 0;
    SANE_Word contrast = 0;
    SANE_Word brightness = 0;
    SANE_Word expiration_time = 0;
    bool custom_gamma = false;

    SANE_Word pos_top_left_y = 0;
    SANE_Word pos_top_left_x = 0;
    SANE_Word pos_bottom_right_y = 0;
    SANE_Word pos_bottom_right_x = 0;

    std::string mode, source, color_filter;

    std::string calibration_file;
    // Button states
    GenesysButton buttons[NUM_BUTTONS];

    // SANE Parameters
    SANE_Parameters params = {};
    SANE_Int bpp_list[5] = {};
};

void write_calibration(std::ostream& str, Genesys_Device::Calibration& cache);
bool read_calibration(std::istream& str, Genesys_Device::Calibration& cache,
                      const std::string& path);

#endif /* not GENESYS_H */

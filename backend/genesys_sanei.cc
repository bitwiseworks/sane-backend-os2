/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Povilas Kanapickas <povilas@radix.lt>

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

#define DEBUG_DECLARE_ONLY

#include "genesys_sanei.h"

UsbDevice::~UsbDevice()
{
    if (is_open()) {
        DBG(DBG_error, "UsbDevice not closed; closing automatically");
        close();
    }
}

void UsbDevice::open(const char* dev_name)
{
    DBG_HELPER(dbg);

    if (is_open()) {
        throw SaneException("device already open");
    }
    int device_num = 0;

    dbg.status("open device");
    TIE(sanei_usb_open(dev_name, &device_num));

    name_ = dev_name;
    device_num_ = device_num;
    is_open_ = true;
}

void UsbDevice::clear_halt()
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_clear_halt(device_num_));
}

void UsbDevice::reset()
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_reset(device_num_));
}

void UsbDevice::close()
{
    DBG_HELPER(dbg);
    assert_is_open();

    // we can't do much if closing fails, so we close the device on our side regardless of the
    // function succeeds
    int device_num = device_num_;

    set_not_open();
    sanei_usb_close(device_num);
}

void UsbDevice::get_vendor_product(int& vendor, int& product)
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_get_vendor_product(device_num_, &vendor, &product));
}

void UsbDevice::control_msg(int rtype, int reg, int value, int index, int length, uint8_t* data)
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_control_msg(device_num_, rtype, reg, value, index, length, data));
}

void UsbDevice::bulk_read(uint8_t* buffer, size_t* size)
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_read_bulk(device_num_, buffer, size));
}

void UsbDevice::bulk_write(const uint8_t* buffer, size_t* size)
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_write_bulk(device_num_, buffer, size));
}

void UsbDevice::assert_is_open() const
{
    if (!is_open()) {
        throw SaneException("device not open");
    }
}

void UsbDevice::set_not_open()
{
    device_num_ = 0;
    is_open_ = false;
    name_ = "";
}

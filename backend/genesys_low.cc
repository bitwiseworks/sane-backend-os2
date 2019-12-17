/* sane - Scanner Access Now Easy.

   Copyright (C) 2010-2013 Stéphane Voltz <stef.dev@free.fr>


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

#include "genesys_low.h"
#include "assert.h"

#include <vector>


Genesys_Device::~Genesys_Device()
{
    clear();

    if (file_name != nullptr)
        free(file_name);
}

void Genesys_Device::clear()
{
    read_buffer.clear();
    lines_buffer.clear();
    shrink_buffer.clear();
    out_buffer.clear();
    binarize_buffer.clear();
    local_buffer.clear();

    calib_file.clear();

    calibration_cache.clear();

    white_average_data.clear();
    dark_average_data.clear();
}

/* ------------------------------------------------------------------------ */
/*                  functions calling ASIC specific functions               */
/* ------------------------------------------------------------------------ */

/**
 * setup the hardware dependent functions
 */
SANE_Status
sanei_genesys_init_cmd_set (Genesys_Device * dev)
{
  DBG_INIT ();
  switch (dev->model->asic_type)
    {
    case GENESYS_GL646:
      return sanei_gl646_init_cmd_set (dev);
    case GENESYS_GL841:
      return sanei_gl841_init_cmd_set (dev);
    case GENESYS_GL843:
      return sanei_gl843_init_cmd_set (dev);
    case GENESYS_GL845: /* since only a few reg bits differs
                           we handle both together */
    case GENESYS_GL846:
      return sanei_gl846_init_cmd_set (dev);
    case GENESYS_GL847:
      return sanei_gl847_init_cmd_set (dev);
    case GENESYS_GL124:
      return sanei_gl124_init_cmd_set (dev);
    default:
      return SANE_STATUS_INVAL;
    }
}

/* ------------------------------------------------------------------------ */
/*                  General IO and debugging functions                      */
/* ------------------------------------------------------------------------ */

SANE_Status sanei_genesys_write_file(const char *filename, uint8_t * data, size_t length)
{
    FILE *out;

#ifdef __OS2__
    out = fopen (filename, "wb");
#else
    out = fopen (filename, "w");
#endif
    if (!out) {
        DBG(DBG_error, "%s: could nor open %s for writing: %s\n", __func__, filename,
            strerror(errno));
        return SANE_STATUS_INVAL;
    }
    fwrite(data, 1, length, out);
    fclose(out);

    DBG(DBG_proc, "%s: finished\n", __func__);
    return SANE_STATUS_GOOD;
}

/* Write data to a pnm file (e.g. calibration). For debugging only */
/* data is RGB or grey, with little endian byte order */
SANE_Status
sanei_genesys_write_pnm_file (const char *filename, uint8_t * data, int depth,
			      int channels, int pixels_per_line, int lines)
{
  FILE *out;
  int count;

  DBG(DBG_info, "%s: depth=%d, channels=%d, ppl=%d, lines=%d\n", __func__,depth, channels,
      pixels_per_line, lines);

#ifdef __OS2__
  out = fopen (filename, "wb");
#else
  out = fopen (filename, "w");
#endif
  if (!out)
    {
      DBG(DBG_error, "%s: could nor open %s for writing: %s\n", __func__, filename,
          strerror(errno));
      return SANE_STATUS_INVAL;
    }
  if(depth==1)
    {
      fprintf (out, "P4\n%d\n%d\n", pixels_per_line, lines);
    }
  else
    {
      fprintf (out, "P%c\n%d\n%d\n%d\n", channels == 1 ? '5' : '6',
	   pixels_per_line, lines, (int) pow (2, depth) - 1);
    }
  if (channels == 3)
    {
      for (count = 0; count < (pixels_per_line * lines * 3); count++)
	{
	  if (depth == 16)
	    fputc (*(data + 1), out);
	  fputc (*(data++), out);
	  if (depth == 16)
	    data++;
	}
    }
  else
    {
      if (depth==1)
        {
          pixels_per_line/=8;
        }
      for (count = 0; count < (pixels_per_line * lines); count++)
	{
          switch (depth)
            {
              case 8:
	        fputc (*(data + count), out);
                break;
              case 16:
	        fputc (*(data + 1), out);
	        fputc (*(data), out);
	        data += 2;
                break;
              default:
                fputc(data[count], out);
                break;
            }
	}
    }
  fclose (out);

  DBG(DBG_proc, "%s: finished\n", __func__);
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------ */
/*                  Read and write RAM, registers and AFE                   */
/* ------------------------------------------------------------------------ */

extern unsigned sanei_genesys_get_bulk_max_size(Genesys_Device * dev)
{
    /*  Genesys supports 0xFE00 maximum size in general, wheraus GL646 supports
        0xFFC0. We use 0xF000 because that's the packet limit in the Linux usbmon
        USB capture stack. By default it limits packet size to b_size / 5 where
        b_size is the size of the ring buffer. By default it's 300*1024, so the
        packet is limited 61440 without any visibility to acquiring software.
    */
    if (dev->model->asic_type == GENESYS_GL124 ||
        dev->model->asic_type == GENESYS_GL846 ||
        dev->model->asic_type == GENESYS_GL847) {
        return 0xeff0;
    }
    return 0xf000;
}

void sanei_genesys_bulk_read_data_send_header(Genesys_Device* dev, size_t len)
{
    DBG_HELPER(dbg);

    uint8_t outdata[8];
    if (dev->model->asic_type == GENESYS_GL124 ||
        dev->model->asic_type == GENESYS_GL846 ||
        dev->model->asic_type == GENESYS_GL847)
    {
        // hard coded 0x10000000 address
        outdata[0] = 0;
        outdata[1] = 0;
        outdata[2] = 0;
        outdata[3] = 0x10;
    } else if (dev->model->asic_type == GENESYS_GL841 ||
               dev->model->asic_type == GENESYS_GL843) {
        outdata[0] = BULK_IN;
        outdata[1] = BULK_RAM;
        outdata[2] = VALUE_BUFFER & 0xff;
        outdata[3] = (VALUE_BUFFER >> 8) & 0xff;
    } else {
        outdata[0] = BULK_IN;
        outdata[1] = BULK_RAM;
        outdata[2] = 0x00;
        outdata[3] = 0x00;
    }

    /* data size to transfer */
    outdata[4] = (len & 0xff);
    outdata[5] = ((len >> 8) & 0xff);
    outdata[6] = ((len >> 16) & 0xff);
    outdata[7] = ((len >> 24) & 0xff);

    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_BUFFER, VALUE_BUFFER, 0x00,
                             sizeof(outdata), outdata);
}

SANE_Status sanei_genesys_bulk_read_data(Genesys_Device * dev, uint8_t addr, uint8_t* data,
                                         size_t len)
{
    DBG_HELPER(dbg);

    // currently supported: GL646, GL841, GL843, GL846, GL847, GL124
    size_t size, target;
    uint8_t *buffer;

    unsigned is_addr_used = 1;
    unsigned has_header_before_each_chunk = 0;
    if (dev->model->asic_type == GENESYS_GL124 ||
        dev->model->asic_type == GENESYS_GL846 ||
        dev->model->asic_type == GENESYS_GL847)
    {
        is_addr_used = 0;
        has_header_before_each_chunk = 1;
    }

    if (is_addr_used) {
        DBG(DBG_io, "%s: requesting %lu bytes from 0x%02x addr\n", __func__, (u_long) len, addr);
    } else {
        DBG(DBG_io, "%s: requesting %lu bytes\n", __func__, (u_long) len);
    }

    if (len == 0)
        return SANE_STATUS_GOOD;

    if (is_addr_used) {
        dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_REGISTER, VALUE_SET_REGISTER, 0x00,
                                 1, &addr);
    }

    target = len;
    buffer = data;

    size_t max_in_size = sanei_genesys_get_bulk_max_size(dev);

    if (!has_header_before_each_chunk) {
        sanei_genesys_bulk_read_data_send_header(dev, len);
    }

    // loop until computed data size is read
    while (target) {
        if (target > max_in_size) {
            size = max_in_size;
        } else {
            size = target;
        }

        if (has_header_before_each_chunk) {
            sanei_genesys_bulk_read_data_send_header(dev, size);
        }

        DBG(DBG_io2, "%s: trying to read %lu bytes of data\n", __func__, (u_long) size);

        dev->usb_dev.bulk_read(data, &size);

        DBG(DBG_io2, "%s: read %lu bytes, %lu remaining\n", __func__,
            (u_long) size, (u_long) (target - size));

        target -= size;
        data += size;
    }

    if (DBG_LEVEL >= DBG_data && dev->binary!=NULL) {
        fwrite(buffer, len, 1, dev->binary);
    }

    return SANE_STATUS_GOOD;
}

SANE_Status sanei_genesys_bulk_write_data(Genesys_Device * dev, uint8_t addr, uint8_t* data,
                                          size_t len)
{
    DBG_HELPER(dbg);

    // supported: GL646, GL841, GL843
    size_t size;
    uint8_t outdata[8];

    DBG(DBG_io, "%s writing %lu bytes\n", __func__, (u_long) len);

    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_REGISTER, VALUE_SET_REGISTER, INDEX,
                             1, &addr);


    size_t max_out_size = sanei_genesys_get_bulk_max_size(dev);

    while (len) {
        if (len > max_out_size)
            size = max_out_size;
        else
            size = len;

        if (dev->model->asic_type == GENESYS_GL841) {
            outdata[0] = BULK_OUT;
            outdata[1] = BULK_RAM;
            outdata[2] = VALUE_BUFFER & 0xff;
            outdata[3] = (VALUE_BUFFER >> 8) & 0xff;
        } else {
            outdata[0] = BULK_OUT;
            outdata[1] = BULK_RAM;
            outdata[2] = 0x00;
            outdata[3] = 0x00;
        }

        outdata[4] = (size & 0xff);
        outdata[5] = ((size >> 8) & 0xff);
        outdata[6] = ((size >> 16) & 0xff);
        outdata[7] = ((size >> 24) & 0xff);

        dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_BUFFER, VALUE_BUFFER, 0x00,
                                 sizeof(outdata), outdata);

        dev->usb_dev.bulk_write(data, &size);

        DBG(DBG_io2, "%s: wrote %lu bytes, %lu remaining\n", __func__, (u_long) size,
            (u_long) (len - size));

        len -= size;
        data += size;
    }

    return SANE_STATUS_GOOD;
}

/** @brief write to one high (addr >= 0x100) register
 * write to a register which address is higher than 0xff.
 * @param dev opened device to write to
 * @param reg LSB of register address
 * @param val value to write
 */
SANE_Status
sanei_genesys_write_hregister (Genesys_Device * dev, uint16_t reg, uint8_t val)
{
    DBG_HELPER(dbg);

  uint8_t buffer[2];

  buffer[0]=reg & 0xff;
  buffer[1]=val;


    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_BUFFER, 0x100 | VALUE_SET_REGISTER, INDEX,
                             2, buffer);

    DBG(DBG_io, "%s (0x%02x, 0x%02x) completed\n", __func__, reg, val);

    return SANE_STATUS_GOOD;
}

/** @brief read from one high (addr >= 0x100) register
 * Read to a register which address is higher than 0xff. Second byte is check to detect
 * physical link errors.
 * @param dev opened device to read from
 * @param reg LSB of register address
 * @param val value to write
 */
SANE_Status
sanei_genesys_read_hregister (Genesys_Device * dev, uint16_t reg, uint8_t * val)
{
    DBG_HELPER(dbg);

  SANE_Byte value[2];

    dev->usb_dev.control_msg(REQUEST_TYPE_IN, REQUEST_BUFFER, 0x100 | VALUE_GET_REGISTER,
                             0x22+((reg & 0xff)<<8), 2, value);

  *val=value[0];
  DBG(DBG_io2, "%s(0x%02x)=0x%02x\n", __func__, reg, *val);

  /* check usb link status */
  if((value[1] & 0xff) != 0x55)
    {
      DBG(DBG_error,"%s: invalid read, scanner unplugged ?\n", __func__);
        return SANE_STATUS_IO_ERROR;
    }
    return SANE_STATUS_GOOD;
}

/**
 * Write to one GL847 ASIC register
URB    10  control  0x40 0x04 0x83 0x00 len     2 wrote 0xa6 0x04
 */
static SANE_Status
sanei_genesys_write_gl847_register (Genesys_Device * dev, uint8_t reg, uint8_t val)
{
    DBG_HELPER(dbg);

  uint8_t buffer[2];

  buffer[0]=reg;
  buffer[1]=val;

    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_BUFFER, VALUE_SET_REGISTER, INDEX,
                             2, buffer);

  DBG(DBG_io, "%s (0x%02x, 0x%02x) completed\n", __func__, reg, val);

    return SANE_STATUS_GOOD;
}

/**
 * Write to one ASIC register
 */
SANE_Status
sanei_genesys_write_register (Genesys_Device * dev, uint16_t reg, uint8_t val)
{
    DBG_HELPER(dbg);

  SANE_Byte reg8;

  /* 16 bit register address space */
  if(reg>255)
    {
      return sanei_genesys_write_hregister(dev, reg, val);
    }

  /* route to gl847 function if needed */
  if(dev->model->asic_type==GENESYS_GL847
  || dev->model->asic_type==GENESYS_GL845
  || dev->model->asic_type==GENESYS_GL846
  || dev->model->asic_type==GENESYS_GL124)
    {
      return sanei_genesys_write_gl847_register(dev, reg, val);
    }

  reg8=reg & 0xff;

    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_REGISTER, VALUE_SET_REGISTER, INDEX,
                             1, &reg8);

    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_REGISTER, VALUE_WRITE_REGISTER, INDEX,
                             1, &val);

  DBG(DBG_io, "%s (0x%02x, 0x%02x) completed\n", __func__, reg, val);

    return SANE_STATUS_GOOD;
}

/**
 * @brief write command to 0x8c endpoint
 * Write a value to 0x8c end point (end access), for USB firmware related operations
 * Known values are 0x0f, 0x11 for USB 2.0 data transfer and 0x0f,0x14 for USB1.1
 * @param dev device to write to
 * @param index index of the command
 * @param val value to write
 */
SANE_Status
sanei_genesys_write_0x8c(Genesys_Device * dev, uint8_t index, uint8_t val)
{
    DBG_HELPER_ARGS(dbg, "0x%02x,0x%02x", index, val);
    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_REGISTER, VALUE_BUF_ENDACCESS, index,
                             1, &val);
    return SANE_STATUS_GOOD;
}

/* read reg 0x41:
 * URB   164  control  0xc0 0x04 0x8e 0x4122 len     2 read  0xfc 0x55
 */
static SANE_Status
sanei_genesys_read_gl847_register (Genesys_Device * dev, uint16_t reg, uint8_t * val)
{
    DBG_HELPER(dbg);
  SANE_Status status = SANE_STATUS_GOOD;
  SANE_Byte value[2];

    dev->usb_dev.control_msg(REQUEST_TYPE_IN, REQUEST_BUFFER, VALUE_GET_REGISTER, 0x22+(reg<<8),
                             2, value);

  *val=value[0];
  DBG(DBG_io2, "%s(0x%02x)=0x%02x\n", __func__, reg, *val);

  /* check usb link status */
  if((value[1] & 0xff) != 0x55)
    {
      DBG(DBG_error,"%s: invalid read, scanner unplugged ?\n", __func__);
      status=SANE_STATUS_IO_ERROR;
    }
  return status;
}

/* Read from one register */
SANE_Status
sanei_genesys_read_register (Genesys_Device * dev, uint16_t reg, uint8_t * val)
{
    DBG_HELPER(dbg);

  SANE_Byte reg8;

  /* 16 bit register address space */
  if(reg>255)
    {
      return sanei_genesys_read_hregister(dev, reg, val);
    }

  /* route to gl847 function if needed */
  if(dev->model->asic_type==GENESYS_GL847
  || dev->model->asic_type==GENESYS_GL845
  || dev->model->asic_type==GENESYS_GL846
  || dev->model->asic_type==GENESYS_GL124)
    return sanei_genesys_read_gl847_register(dev, reg, val);

  /* 8 bit register address space */
    reg8=(SANE_Byte)(reg& 0Xff);

    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_REGISTER, VALUE_SET_REGISTER, INDEX,
                             1, &reg8);

  *val = 0;

    dev->usb_dev.control_msg(REQUEST_TYPE_IN, REQUEST_REGISTER, VALUE_READ_REGISTER, INDEX,
                             1, val);

  DBG(DBG_io, "%s (0x%02x, 0x%02x) completed\n", __func__, reg, *val);

    return SANE_STATUS_GOOD;
}

/* Set address for writing data */
SANE_Status
sanei_genesys_set_buffer_address (Genesys_Device * dev, uint32_t addr)
{
  SANE_Status status = SANE_STATUS_GOOD;

  if(dev->model->asic_type==GENESYS_GL847
  || dev->model->asic_type==GENESYS_GL845
  || dev->model->asic_type==GENESYS_GL846
  || dev->model->asic_type==GENESYS_GL124)
    {
      DBG(DBG_warn, "%s: shouldn't be used for GL846+ ASICs\n", __func__);
      return SANE_STATUS_GOOD;
    }

  DBG(DBG_io, "%s: setting address to 0x%05x\n", __func__, addr & 0xfffffff0);

  addr = addr >> 4;

  status = sanei_genesys_write_register (dev, 0x2b, (addr & 0xff));
  if (status != SANE_STATUS_GOOD)
    {
      DBG(DBG_error, "%s: failed while writing low byte: %s\n", __func__, sane_strstatus(status));
      return status;
    }

  addr = addr >> 8;
  status = sanei_genesys_write_register (dev, 0x2a, (addr & 0xff));
  if (status != SANE_STATUS_GOOD)
    {
      DBG(DBG_error, "%s: failed while writing high byte: %s\n", __func__, sane_strstatus(status));
      return status;
    }

  DBG(DBG_io, "%s: completed\n", __func__);

  return status;
}

/**@brief read data from analog frontend (AFE)
 * @param dev device owning the AFE
 * @param addr register address to read
 * @param data placeholder for the result
 * @return SANE_STATUS_GOOD is OK, else the error code
 */
SANE_Status
sanei_genesys_fe_read_data (Genesys_Device * dev, uint8_t addr,
			     uint16_t *data)
{
  SANE_Status status = SANE_STATUS_GOOD;
  uint8_t value;
  Genesys_Register_Set reg;


  DBG(DBG_proc, "%s: start\n", __func__);

  reg.init_reg(0x50, addr);

  /* set up read address */
  status = dev->model->cmd_set->bulk_write_register(dev, reg);
  if (status != SANE_STATUS_GOOD)
    {
      DBG(DBG_error, "%s: failed while bulk writing registers: %s\n", __func__,
          sane_strstatus(status));
      return status;
    }

  /* read data */
  RIE (sanei_genesys_read_register (dev, 0x46, &value));
  *data=256*value;
  RIE (sanei_genesys_read_register (dev, 0x47, &value));
  *data+=value;

  DBG(DBG_io, "%s (0x%02x, 0x%04x)\n", __func__, addr, *data);
  DBG(DBG_proc, "%s: completed\n", __func__);

  return status;
}

/*@brief write data to analog frontend
 * writes data to analog frontend to set it up accordingly
 * to the sensor settings (exposure, timings, color, bit depth, ...)
 * @param dev devie owning the AFE to write to
 * @param addr AFE rister address
 * @param data value to write to AFE register
 **/
SANE_Status
sanei_genesys_fe_write_data (Genesys_Device * dev, uint8_t addr,
			     uint16_t data)
{
  SANE_Status status = SANE_STATUS_GOOD;
  Genesys_Register_Set reg(Genesys_Register_Set::SEQUENTIAL);

  DBG(DBG_io, "%s (0x%02x, 0x%04x)\n", __func__, addr, data);

    reg.init_reg(0x51, addr);
    if (dev->model->asic_type == GENESYS_GL124) {
        reg.init_reg(0x5d, (data / 256) & 0xff);
        reg.init_reg(0x5e, data & 0xff);
    } else {
        reg.init_reg(0x3a, (data / 256) & 0xff);
        reg.init_reg(0x3b, data & 0xff);
    }

  status = dev->model->cmd_set->bulk_write_register(dev, reg);
  if (status != SANE_STATUS_GOOD)
    {
      DBG(DBG_error, "%s: failed while bulk writing registers: %s\n", __func__,
          sane_strstatus(status));
      return status;
    }

  DBG(DBG_io, "%s: completed\n", __func__);

  return status;
}

/* ------------------------------------------------------------------------ */
/*                       Medium level functions                             */
/* ------------------------------------------------------------------------ */

/** read the status register
 */
SANE_Status
sanei_genesys_get_status (Genesys_Device * dev, uint8_t * status)
{
  if(dev->model->asic_type==GENESYS_GL124)
    return sanei_genesys_read_hregister(dev, 0x101, status);
  return sanei_genesys_read_register (dev, 0x41, status);
}

/**
 * decodes and prints content of status register
 * @param val value read from status register
 */
void sanei_genesys_print_status (uint8_t val)
{
  char msg[80];

  sprintf (msg, "%s%s%s%s%s%s%s%s",
	   val & PWRBIT ? "PWRBIT " : "",
	   val & BUFEMPTY ? "BUFEMPTY " : "",
	   val & FEEDFSH ? "FEEDFSH " : "",
	   val & SCANFSH ? "SCANFSH " : "",
	   val & HOMESNR ? "HOMESNR " : "",
	   val & LAMPSTS ? "LAMPSTS " : "",
	   val & FEBUSY ? "FEBUSY " : "",
	   val & MOTORENB ? "MOTORENB" : "");
  DBG(DBG_info, "status=%s\n", msg);
}

#if 0
/* returns pixels per line from register set */
/*candidate for moving into chip specific files?*/
static int
genesys_pixels_per_line (Genesys_Register_Set * reg)
{
  int pixels_per_line;

  pixels_per_line =
    sanei_genesys_read_reg_from_set (reg,
				     0x32) * 256 +
    sanei_genesys_read_reg_from_set (reg, 0x33);
  pixels_per_line -=
    (sanei_genesys_read_reg_from_set (reg, 0x30) * 256 +
     sanei_genesys_read_reg_from_set (reg, 0x31));

  return pixels_per_line;
}

/* returns dpiset from register set */
/*candidate for moving into chip specific files?*/
static int
genesys_dpiset (Genesys_Register_Set * reg)
{
  int dpiset;

  dpiset =
    sanei_genesys_read_reg_from_set (reg,
				     0x2c) * 256 +
    sanei_genesys_read_reg_from_set (reg, 0x2d);

  return dpiset;
}
#endif

/** read the number of valid words in scanner's RAM
 * ie registers 42-43-44
 */
/*candidate for moving into chip specific files?*/
SANE_Status
sanei_genesys_read_valid_words (Genesys_Device * dev, unsigned int *words)
{
  SANE_Status status = SANE_STATUS_GOOD;
  uint8_t value;

  DBGSTART;
  switch (dev->model->asic_type)
    {
    case GENESYS_GL124:
      RIE (sanei_genesys_read_hregister (dev, 0x102, &value));
      *words = (value & 0x03);
      RIE (sanei_genesys_read_hregister (dev, 0x103, &value));
      *words = *words * 256 + value;
      RIE (sanei_genesys_read_hregister (dev, 0x104, &value));
      *words = *words * 256 + value;
      RIE (sanei_genesys_read_hregister (dev, 0x105, &value));
      *words = *words * 256 + value;
      break;

    case GENESYS_GL845:
    case GENESYS_GL846:
      RIE (sanei_genesys_read_register (dev, 0x42, &value));
      *words = (value & 0x02);
      RIE (sanei_genesys_read_register (dev, 0x43, &value));
      *words = *words * 256 + value;
      RIE (sanei_genesys_read_register (dev, 0x44, &value));
      *words = *words * 256 + value;
      RIE (sanei_genesys_read_register (dev, 0x45, &value));
      *words = *words * 256 + value;
      break;

    case GENESYS_GL847:
      RIE (sanei_genesys_read_register (dev, 0x42, &value));
      *words = (value & 0x03);
      RIE (sanei_genesys_read_register (dev, 0x43, &value));
      *words = *words * 256 + value;
      RIE (sanei_genesys_read_register (dev, 0x44, &value));
      *words = *words * 256 + value;
      RIE (sanei_genesys_read_register (dev, 0x45, &value));
      *words = *words * 256 + value;
      break;

    default:
      RIE (sanei_genesys_read_register (dev, 0x44, &value));
      *words = value;
      RIE (sanei_genesys_read_register (dev, 0x43, &value));
      *words += (value * 256);
      RIE (sanei_genesys_read_register (dev, 0x42, &value));
      if (dev->model->asic_type == GENESYS_GL646)
	*words += ((value & 0x03) * 256 * 256);
      else
	*words += ((value & 0x0f) * 256 * 256);
    }

  DBG(DBG_proc, "%s: %d words\n", __func__, *words);
  DBGCOMPLETED;
  return SANE_STATUS_GOOD;
}

/** read the number of lines scanned
 * ie registers 4b-4c-4d
 */
SANE_Status
sanei_genesys_read_scancnt (Genesys_Device * dev, unsigned int *words)
{
  SANE_Status status = SANE_STATUS_GOOD;
  uint8_t value;

  DBG(DBG_proc, "%s: start\n", __func__);

  if (dev->model->asic_type == GENESYS_GL124)
    {
      RIE (sanei_genesys_read_hregister (dev, 0x10b, &value));
      *words = (value & 0x0f) << 16;
      RIE (sanei_genesys_read_hregister (dev, 0x10c, &value));
      *words += (value << 8);
      RIE (sanei_genesys_read_hregister (dev, 0x10d, &value));
      *words += value;
    }
  else
    {
      RIE (sanei_genesys_read_register (dev, 0x4d, &value));
      *words = value;
      RIE (sanei_genesys_read_register (dev, 0x4c, &value));
      *words += (value * 256);
      RIE (sanei_genesys_read_register (dev, 0x4b, &value));
      if (dev->model->asic_type == GENESYS_GL646)
        *words += ((value & 0x03) * 256 * 256);
      else
        *words += ((value & 0x0f) * 256 * 256);
    }

  DBG(DBG_proc, "%s: %d lines\n", __func__, *words);
  return SANE_STATUS_GOOD;
}

/** @brief Check if the scanner's internal data buffer is empty
 * @param *dev device to test for data
 * @param *empty return value
 * @return empty will be set to SANE_TRUE if there is no scanned data.
 **/
SANE_Status
sanei_genesys_test_buffer_empty (Genesys_Device * dev, SANE_Bool * empty)
{
  uint8_t val = 0;
  SANE_Status status = SANE_STATUS_GOOD;

  sanei_genesys_sleep_ms(1);
  status = sanei_genesys_get_status (dev, &val);
  if (status != SANE_STATUS_GOOD)
    {
      DBG(DBG_error, "%s: failed to read buffer status: %s\n", __func__, sane_strstatus(status));
      return status;
    }

  if (dev->model->cmd_set->test_buffer_empty_bit (val))
    {
      /* fix timing issue on USB3 (or just may be too fast) hardware
       * spotted by John S. Weber <jweber53@gmail.com>
       */
      sanei_genesys_sleep_ms(1);
      DBG(DBG_io2, "%s: buffer is empty\n", __func__);
      *empty = SANE_TRUE;
      return SANE_STATUS_GOOD;
    }

  *empty = SANE_FALSE;

  DBG(DBG_io, "%s: buffer is filled\n", __func__);
  return SANE_STATUS_GOOD;
}


/* Read data (e.g scanned image) from scan buffer */
SANE_Status
sanei_genesys_read_data_from_scanner (Genesys_Device * dev, uint8_t * data,
				      size_t size)
{
  SANE_Status status = SANE_STATUS_GOOD;
  int time_count = 0;
  unsigned int words = 0;

  DBG(DBG_proc, "%s (size = %lu bytes)\n", __func__, (u_long) size);

  if (size & 1)
    DBG(DBG_info, "WARNING %s: odd number of bytes\n", __func__);

  /* wait until buffer not empty for up to 5 seconds */
  do
    {
      status = sanei_genesys_read_valid_words (dev, &words);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG(DBG_error, "%s: checking for empty buffer failed: %s\n", __func__,
	      sane_strstatus(status));
	  return status;
	}
      if (words == 0)
	{
          sanei_genesys_sleep_ms(10);
	  time_count++;
	}
    }
  while ((time_count < 2500*2) && (words == 0));

  if (words == 0)		/* timeout, buffer does not get filled */
    {
      DBG(DBG_error, "%s: timeout, buffer does not get filled\n", __func__);
      return SANE_STATUS_IO_ERROR;
    }

  status = dev->model->cmd_set->bulk_read_data (dev, 0x45, data, size);
  if (status != SANE_STATUS_GOOD)
    {
      DBG(DBG_error, "%s: reading bulk data failed: %s\n", __func__, sane_strstatus(status));
      return status;
    }

  DBG(DBG_proc, "%s: completed\n", __func__);
  return SANE_STATUS_GOOD;
}
SANE_Status
sanei_genesys_read_feed_steps (Genesys_Device * dev, unsigned int *steps)
{
  SANE_Status status = SANE_STATUS_GOOD;
  uint8_t value;

  DBG(DBG_proc, "%s\n", __func__);

  if (dev->model->asic_type == GENESYS_GL124)
    {
      RIE (sanei_genesys_read_hregister (dev, 0x108, &value));
      *steps = (value & 0x1f) << 16;
      RIE (sanei_genesys_read_hregister (dev, 0x109, &value));
      *steps += (value << 8);
      RIE (sanei_genesys_read_hregister (dev, 0x10a, &value));
      *steps += value;
    }
  else
    {
      RIE (sanei_genesys_read_register (dev, 0x4a, &value));
      *steps = value;
      RIE (sanei_genesys_read_register (dev, 0x49, &value));
      *steps += (value * 256);
      RIE (sanei_genesys_read_register (dev, 0x48, &value));
      if (dev->model->asic_type == GENESYS_GL646)
        *steps += ((value & 0x03) * 256 * 256);
      else if (dev->model->asic_type == GENESYS_GL841)
        *steps += ((value & 0x0f) * 256 * 256);
      else
        *steps += ((value & 0x1f) * 256 * 256);
    }

  DBG(DBG_proc, "%s: %d steps\n", __func__, *steps);
  return SANE_STATUS_GOOD;
}

void sanei_genesys_set_lamp_power(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                  Genesys_Register_Set& regs, bool set)
{
    static const uint8_t REG03_LAMPPWR = 0x10;

    if (set) {
        regs.find_reg(0x03).value |= REG03_LAMPPWR;

        if (dev->model->asic_type == GENESYS_GL841) {
            sanei_genesys_set_exposure(regs, sanei_genesys_fixup_exposure(sensor.exposure));
            regs.set8(0x19, 0x50);
        }

        if (dev->model->asic_type == GENESYS_GL843) {
            sanei_genesys_set_exposure(regs, sensor.exposure);
        }
    } else {
        regs.find_reg(0x03).value &= ~REG03_LAMPPWR;

        if (dev->model->asic_type == GENESYS_GL841) {
            sanei_genesys_set_exposure(regs, {0x0101, 0x0101, 0x0101});
            regs.set8(0x19, 0xff);
        }

        if (dev->model->asic_type == GENESYS_GL843) {
            if (dev->model->model_id != MODEL_CANON_CANOSCAN_8600F) {
                // BUG: datasheet says we shouldn't set exposure to zero
                sanei_genesys_set_exposure(regs, {0, 0, 0});
            }
        }
    }
    regs.state.is_lamp_on = set;
}

void sanei_genesys_set_motor_power(Genesys_Register_Set& regs, bool set)
{
    static const uint8_t REG02_MTRPWR = 0x10;

    if (set) {
        regs.find_reg(0x02).value |= REG02_MTRPWR;
    } else {
        regs.find_reg(0x02).value &= ~REG02_MTRPWR;
    }
}

/**
 * Write to many registers at once
 * Note: sequential call to write register, no effective
 * bulk write implemented.
 * @param dev device to write to
 * @param reg pointer to an array of registers
 * @param elems size of the array
 */
SANE_Status sanei_genesys_bulk_write_register(Genesys_Device * dev, Genesys_Register_Set& reg)
{
    DBG_HELPER(dbg);

    SANE_Status status = SANE_STATUS_GOOD;

    if (dev->model->asic_type == GENESYS_GL646 ||
        dev->model->asic_type == GENESYS_GL841)
    {
        uint8_t outdata[8];
        std::vector<uint8_t> buffer;
        buffer.reserve(reg.size() * 2);

        /* copy registers and values in data buffer */
        for (const auto& r : reg) {
            buffer.push_back(r.address);
            buffer.push_back(r.value);
        }

        DBG(DBG_io, "%s (elems= %lu, size = %lu)\n", __func__, (u_long) reg.size(),
            (u_long) buffer.size());

        if (dev->model->asic_type == GENESYS_GL646) {
            outdata[0] = BULK_OUT;
            outdata[1] = BULK_REGISTER;
            outdata[2] = 0x00;
            outdata[3] = 0x00;
            outdata[4] = (buffer.size() & 0xff);
            outdata[5] = ((buffer.size() >> 8) & 0xff);
            outdata[6] = ((buffer.size() >> 16) & 0xff);
            outdata[7] = ((buffer.size() >> 24) & 0xff);

            dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_BUFFER, VALUE_BUFFER, INDEX,
                                     sizeof(outdata), outdata);

            size_t write_size = buffer.size();

            dev->usb_dev.bulk_write(buffer.data(), &write_size);
        } else {
            for (size_t i = 0; i < reg.size();) {
                size_t c = reg.size() - i;
                if (c > 32)  /*32 is max on GL841. checked that.*/
                    c = 32;

                dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_BUFFER, VALUE_SET_REGISTER,
                                         INDEX, c * 2, buffer.data() + i * 2);

                i += c;
            }
        }
    } else {
        for (const auto& r : reg) {
            status = sanei_genesys_write_register (dev, r.address, r.value);
            if (status != SANE_STATUS_GOOD)
                return status;
        }
    }

    DBG (DBG_io, "%s: wrote %lu registers\n", __func__, (u_long) reg.size());
    return status;
}



/**
 * writes a block of data to AHB
 * @param dn USB device index
 * @param usb_mode usb mode : 1 usb 1.1, 2 usb 2.0
 * @param addr AHB address to write to
 * @param size size of the chunk of data
 * @param data pointer to the data to write
 */
SANE_Status
sanei_genesys_write_ahb(Genesys_Device* dev, uint32_t addr, uint32_t size, uint8_t * data)
{
    DBG_HELPER(dbg);

  uint8_t outdata[8];
  size_t written,blksize;
  SANE_Status status = SANE_STATUS_GOOD;
  int i;
  char msg[100]="AHB=";

  outdata[0] = addr & 0xff;
  outdata[1] = ((addr >> 8) & 0xff);
  outdata[2] = ((addr >> 16) & 0xff);
  outdata[3] = ((addr >> 24) & 0xff);
  outdata[4] = (size & 0xff);
  outdata[5] = ((size >> 8) & 0xff);
  outdata[6] = ((size >> 16) & 0xff);
  outdata[7] = ((size >> 24) & 0xff);

  if (DBG_LEVEL >= DBG_io)
    {
      for (i = 0; i < 8; i++)
	{
          sprintf (msg+strlen(msg), " 0x%02x", outdata[i]);
	}
      DBG (DBG_io, "%s: write(0x%08x,0x%08x)\n", __func__, addr,size);
      DBG (DBG_io, "%s: %s\n", __func__, msg);
    }

    // write addr and size for AHB
    dev->usb_dev.control_msg(REQUEST_TYPE_OUT, REQUEST_BUFFER, VALUE_BUFFER, 0x01, 8, outdata);

  size_t max_out_size = sanei_genesys_get_bulk_max_size(dev);

  /* write actual data */
  written = 0;
  do
    {
      if (size - written > max_out_size)
        {
          blksize = max_out_size;
        }
      else
        {
          blksize = size - written;
        }
        dev->usb_dev.bulk_write(data + written, &blksize);

      written += blksize;
    }
  while (written < size);

  return status;
}


std::vector<uint16_t> get_gamma_table(Genesys_Device* dev, const Genesys_Sensor& sensor,
                                      int color)
{
    if (!dev->gamma_override_tables[color].empty()) {
        return dev->gamma_override_tables[color];
    } else {
        std::vector<uint16_t> ret;
        sanei_genesys_create_default_gamma_table(dev, ret, sensor.gamma[color]);
        return ret;
    }
}

/** @brief generates gamma buffer to transfer
 * Generates gamma table buffer to send to ASIC. Applies
 * contrast and brightness if set.
 * @param dev device to set up
 * @param bits number of bits used by gamma
 * @param max value for gamma
 * @param size of the gamma table
 * @param gamma allocated gamma buffer to fill
 * @returns SANE_STATUS_GOOD or SANE_STATUS_NO_MEM
 */
SANE_Status sanei_genesys_generate_gamma_buffer(Genesys_Device * dev,
                                                const Genesys_Sensor& sensor,
                                                int bits,
                                                int max,
                                                int size,
                                                uint8_t *gamma)
{
    std::vector<uint16_t> rgamma = get_gamma_table(dev, sensor, GENESYS_RED);
    std::vector<uint16_t> ggamma = get_gamma_table(dev, sensor, GENESYS_GREEN);
    std::vector<uint16_t> bgamma = get_gamma_table(dev, sensor, GENESYS_BLUE);

  if(dev->settings.contrast!=0 || dev->settings.brightness!=0)
    {
      std::vector<uint16_t> lut(65536);
      sanei_genesys_load_lut((unsigned char *)lut.data(),
                             bits,
                             bits,
                             0,
                             max,
                             dev->settings.contrast,
                             dev->settings.brightness);
      for (int i = 0; i < size; i++)
        {
          uint16_t value=rgamma[i];
          value=lut[value];
          gamma[i * 2 + size * 0 + 0] = value & 0xff;
          gamma[i * 2 + size * 0 + 1] = (value >> 8) & 0xff;

          value=ggamma[i];
          value=lut[value];
          gamma[i * 2 + size * 2 + 0] = value & 0xff;
          gamma[i * 2 + size * 2 + 1] = (value >> 8) & 0xff;

          value=bgamma[i];
          value=lut[value];
          gamma[i * 2 + size * 4 + 0] = value & 0xff;
          gamma[i * 2 + size * 4 + 1] = (value >> 8) & 0xff;
        }
    }
  else
    {
      for (int i = 0; i < size; i++)
        {
          uint16_t value=rgamma[i];
          gamma[i * 2 + size * 0 + 0] = value & 0xff;
          gamma[i * 2 + size * 0 + 1] = (value >> 8) & 0xff;

          value=ggamma[i];
          gamma[i * 2 + size * 2 + 0] = value & 0xff;
          gamma[i * 2 + size * 2 + 1] = (value >> 8) & 0xff;

          value=bgamma[i];
          gamma[i * 2 + size * 4 + 0] = value & 0xff;
          gamma[i * 2 + size * 4 + 1] = (value >> 8) & 0xff;
        }
    }

  return SANE_STATUS_GOOD;
}


/** @brief send gamma table to scanner
 * This function sends generic gamma table (ie ones built with
 * provided gamma) or the user defined one if provided by
 * fontend. Used by gl846+ ASICs
 * @param dev device to write to
 */
SANE_Status
sanei_genesys_send_gamma_table(Genesys_Device * dev, const Genesys_Sensor& sensor)
{
  int size;
  int i;
  uint8_t val;
  SANE_Status status = SANE_STATUS_GOOD;

  DBGSTART;

  size = 256 + 1;

  /* allocate temporary gamma tables: 16 bits words, 3 channels */
  std::vector<uint8_t> gamma(size * 2 * 3, 255);

  RIE(sanei_genesys_generate_gamma_buffer(dev, sensor, 16, 65535, size, gamma.data()));

  /* loop sending gamma tables NOTE: 0x01000000 not 0x10000000 */
  for (i = 0; i < 3; i++)
    {
      /* clear corresponding GMM_N bit */
      RIE(sanei_genesys_read_register(dev, 0xbd, &val));
      val &= ~(0x01 << i);
      RIE(sanei_genesys_write_register(dev, 0xbd, val));

      /* clear corresponding GMM_F bit */
      RIE(sanei_genesys_read_register(dev, 0xbe, &val));
      val &= ~(0x01 << i);
      RIE(sanei_genesys_write_register(dev, 0xbe, val));

      // FIXME: currently the last word of each gamma table is not initialied, so to work around
      // unstable data, just set it to 0 which is the most likely value of uninitialized memory
      // (proper value is probably 0xff)
      gamma[size * 2 * i + size * 2 - 2] = 0;
      gamma[size * 2 * i + size * 2 - 1] = 0;

      /* set GMM_Z */
      RIE(sanei_genesys_write_register (dev, 0xc5+2*i, gamma[size*2*i+1]));
      RIE(sanei_genesys_write_register (dev, 0xc6+2*i, gamma[size*2*i]));

      status = sanei_genesys_write_ahb(dev, 0x01000000 + 0x200 * i, (size-1) * 2, gamma.data() + i * size * 2+2);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (DBG_error,
	       "%s: write to AHB failed writing table %d (%s)\n", __func__,
	       i, sane_strstatus (status));
          break;
	}
    }

  DBGCOMPLETED;
  return status;
}

/** @brief initialize device
 * Initialize backend and ASIC : registers, motor tables, and gamma tables
 * then ensure scanner's head is at home. Designed for gl846+ ASICs.
 * Detects cold boot (ie first boot since device plugged) in this case
 * an extensice setup up is done at hardware level.
 *
 * @param dev device to initialize
 * @param max_regs umber of maximum used registers
 * @return SANE_STATUS_GOOD in case of success
 */
SANE_Status
sanei_genesys_asic_init(Genesys_Device* dev, int /*max_regs*/)
{
    DBG_HELPER(dbg);

  SANE_Status status = SANE_STATUS_GOOD;
  uint8_t val;
  SANE_Bool cold = SANE_TRUE;

  DBGSTART;

    // URB    16  control  0xc0 0x0c 0x8e 0x0b len     1 read  0x00 */
    dev->usb_dev.control_msg(REQUEST_TYPE_IN, REQUEST_REGISTER, VALUE_GET_REGISTER, 0x00, 1, &val);

  DBG (DBG_io2, "%s: value=0x%02x\n", __func__, val);
  DBG (DBG_info, "%s: device is %s\n", __func__, (val & 0x08) ? "USB 1.0" : "USB2.0");
  if (val & 0x08)
    {
      dev->usb_mode = 1;
    }
  else
    {
      dev->usb_mode = 2;
    }

  /* check if the device has already been initialized and powered up
   * we read register 6 and check PWRBIT, if reset scanner has been
   * freshly powered up. This bit will be set to later so that following
   * reads can detect power down/up cycle*/
  RIE (sanei_genesys_read_register (dev, 0x06, &val));
  /* test for POWER bit */
  if (val & 0x10)
    {
      cold = SANE_FALSE;
    }
  DBG (DBG_info, "%s: device is %s\n", __func__, cold ? "cold" : "warm");

  /* don't do anything if backend is initialized and hardware hasn't been
   * replug */
  if (dev->already_initialized && !cold)
    {
      DBG (DBG_info, "%s: already initialized, nothing to do\n", __func__);
      return SANE_STATUS_GOOD;
    }

  /* set up hardware and registers */
  RIE (dev->model->cmd_set->asic_boot (dev, cold));

  /* now hardware part is OK, set up device struct */
  dev->white_average_data.clear();
  dev->dark_average_data.clear();

  dev->settings.color_filter = ColorFilter::RED;

  /* duplicate initial values into calibration registers */
  dev->calib_reg = dev->reg;

  const auto& sensor = sanei_genesys_find_sensor_any(dev);

  /* Set analog frontend */
  RIE (dev->model->cmd_set->set_fe(dev, sensor, AFE_INIT));

  dev->already_initialized = SANE_TRUE;

  /* Move to home if needed */
  RIE (dev->model->cmd_set->slow_back_home (dev, SANE_TRUE));
  dev->scanhead_position_in_steps = 0;

  /* Set powersaving (default = 15 minutes) */
  RIE (dev->model->cmd_set->set_powersaving (dev, 15));

    return status;
}

/**
 * Wait for the scanning head to park
 */
SANE_Status
sanei_genesys_wait_for_home (Genesys_Device * dev)
{
  SANE_Status status = SANE_STATUS_GOOD;
  uint8_t val;
  int loop;
  int max=300;

  DBGSTART;

  /* clear the parking status whatever the outcome of the function */
  dev->parking=SANE_FALSE;

  /* read initial status, if head isn't at home and motor is on
   * we are parking, so we wait.
   * gl847/gl124 need 2 reads for reliable results */
  status = sanei_genesys_get_status (dev, &val);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (DBG_error,
	   "%s: failed to read home sensor: %s\n", __func__,
	   sane_strstatus (status));
      return status;
    }
  sanei_genesys_sleep_ms(10);
  status = sanei_genesys_get_status (dev, &val);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (DBG_error,
	   "%s: failed to read home sensor: %s\n", __func__,
	   sane_strstatus (status));
      return status;
    }

  /* if at home, return */
  if(val & HOMESNR)
    {
	  DBG (DBG_info,
	       "%s: already at home\n", __func__);
	  return status;
    }

  /* loop for 30 s max, polling home sensor */
  loop = 0;
  do
    {
      sanei_genesys_sleep_ms(100);
      status = sanei_genesys_get_status (dev, &val);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (DBG_error,
	       "%s: failed to read home sensor: %s\n", __func__,
	       sane_strstatus (status));
	  return status;
	}
          if (DBG_LEVEL >= DBG_io2)
            {
              sanei_genesys_print_status (val);
            }
      ++loop;
    }
  while (loop < max && !(val & HOMESNR) && status == SANE_STATUS_GOOD);

  /* if after the timeout, head is still not parked, error out */
  if(loop >= max && !(val & HOMESNR) && status == SANE_STATUS_GOOD)
    {
      DBG (DBG_error, "%s: failed to reach park position %ds\n", __func__, max/10);
      return SANE_STATUS_IO_ERROR;
    }

  DBGCOMPLETED;
  return status;
}

/**@brief compute hardware sensor dpi to use
 * compute the sensor hardware dpi based on target resolution.
 * A lower dpihw enable faster scans.
 * @param dev device used for the scan
 * @param xres x resolution of the scan
 * @return the hardware dpi to use
 */
int sanei_genesys_compute_dpihw(Genesys_Device *dev, const Genesys_Sensor& sensor, int xres)
{
  /* some scanners use always hardware dpi for sensor */
  if (dev->model->flags & GENESYS_FLAG_FULL_HWDPI_MODE)
    {
      return sensor.optical_res;
    }

  /* can't be below 600 dpi */
  if (xres <= 600)
    {
      return 600;
    }
  if (xres <= sensor.optical_res / 4)
    {
      return sensor.optical_res / 4;
    }
  if (xres <= sensor.optical_res / 2)
    {
      return sensor.optical_res / 2;
    }
  return sensor.optical_res;
}

// sanei_genesys_compute_dpihw returns the dpihw that is written to register.
// However the number of pixels depends on half_ccd mode
int sanei_genesys_compute_dpihw_calibration(Genesys_Device *dev, const Genesys_Sensor& sensor,
                                            int xres)
{
  if (dev->model->model_id == MODEL_CANON_CANOSCAN_8600F)
    {
      // real resolution is half of the "official" resolution - half_ccd mode
      int hwres = sensor.optical_res / sensor.get_ccd_size_divisor_for_dpi(xres);

      if (xres <= hwres / 4)
        {
          return hwres / 4;
        }
      if (xres <= hwres / 2)
        {
          return hwres / 2;
        }
      return hwres;
    }

  return sanei_genesys_compute_dpihw(dev, sensor, xres);
}

/** @brief motor profile
 * search for the database of motor profiles and get the best one. Each
 * profile is at full step and at a reference exposure. Use first entry
 * by default.
 * @param motors motor profile database
 * @param motor_type motor id
 * @param exposure exposure time
 * @return a pointer to a Motor_Profile struct
 */
Motor_Profile *sanei_genesys_get_motor_profile(Motor_Profile *motors, int motor_type, int exposure)
{
  unsigned int i;
  int idx;

  i=0;
  idx=-1;
  while(motors[i].exposure!=0)
    {
      /* exact match */
      if(motors[i].motor_type==motor_type && motors[i].exposure==exposure)
        {
          return &(motors[i]);
        }

      /* closest match */
      if(motors[i].motor_type==motor_type)
        {
          /* if profile exposure is higher than the required one,
           * the entry is a candidate for the closest match */
          if(motors[i].exposure>=exposure)
            {
              if(idx<0)
                {
                  /* no match found yet */
                  idx=i;
                }
              else
                {
                  /* test for better match */
                  if(motors[i].exposure<motors[idx].exposure)
                    {
                      idx=i;
                    }
                }
            }
        }
      i++;
    }

  /* default fallback */
  if(idx<0)
    {
      DBG (DBG_warn,"%s: using default motor profile\n",__func__);
      idx=0;
    }

  return &(motors[idx]);
}

/**@brief compute motor step type to use
 * compute the step type (full, half, quarter, ...) to use based
 * on target resolution
 * @param motors motor profile database
 * @param motor_type motor id
 * @param exposure sensor exposure
 * @return 0 for full step
 *         1 for half step
 *         2 for quarter step
 *         3 for eighth step
 */
int sanei_genesys_compute_step_type(Motor_Profile *motors,
                                    int motor_type,
                                    int exposure)
{
Motor_Profile *profile;

    profile=sanei_genesys_get_motor_profile(motors, motor_type, exposure);
    return profile->step_type;
}

/** @brief generate slope table
 * Generate the slope table to use for the scan using a reference slope
 * table.
 * @param slope pointer to the slope table to fill
 * @param steps pointer to return used step number
 * @param dpi   desired motor resolution
 * @param exposure exposure used
 * @param base_dpi base resolution of the motor
 * @param step_type step type used for scan
 * @param factor shrink factor for the slope
 * @param motor_type motor id
 * @param motors motor profile database
 */
int sanei_genesys_slope_table(uint16_t *slope,
		             int       *steps,
			     int       dpi,
			     int       exposure,
			     int       base_dpi,
			     int       step_type,
			     int       factor,
                             int       motor_type,
                             Motor_Profile *motors)
{
int sum, i;
uint16_t target,current;
Motor_Profile *profile;

	/* required speed */
	target=((exposure * dpi) / base_dpi)>>step_type;
        DBG (DBG_io2, "%s: exposure=%d, dpi=%d, target=%d\n", __func__, exposure, dpi, target);

	/* fill result with target speed */
        for(i=0;i<SLOPE_TABLE_SIZE;i++)
          slope[i]=target;

        profile=sanei_genesys_get_motor_profile(motors, motor_type, exposure);

	/* use profile to build table */
        i=0;
	sum=0;

        /* first step is always used unmodified */
        current=profile->table[0];

        /* loop on profile copying and apply step type */
        while(profile->table[i]!=0 && current>=target)
          {
            slope[i]=current;
            sum+=slope[i];
            i++;
            current=profile->table[i]>>step_type;
          }

        /* ensure last step is required speed in case profile doesn't contain it */
        if(current!=0 && current<target)
          {
            slope[i]=target;
            sum+=slope[i];
            i++;
          }

        /* range checking */
        if(profile->table[i]==0 && DBG_LEVEL >= DBG_warn && current>target)
          {
            DBG (DBG_warn,"%s: short slope table, failed to reach %d. target too low ?\n",__func__,target);
          }
        if(i<3 && DBG_LEVEL >= DBG_warn)
          {
            DBG (DBG_warn,"%s: short slope table, failed to reach %d. target too high ?\n",__func__,target);
          }

        /* align on factor */
        while(i%factor!=0)
          {
            slope[i+1]=slope[i];
            sum+=slope[i];
            i++;
          }

        /* ensure minimal slope size */
        while(i<2*factor)
          {
            slope[i+1]=slope[i];
            sum+=slope[i];
            i++;
          }

        // return used steps and taken time
        *steps=i/factor;
	return sum;
}

/** @brief returns the lowest possible ydpi for the device
 * Parses device entry to find lowest motor dpi.
 * @param dev device description
 * @return lowest motor resolution
 */
int sanei_genesys_get_lowest_ydpi(Genesys_Device *dev)
{
  int min=20000;
  int i=0;

  while(dev->model->ydpi_values[i]!=0)
    {
      if(dev->model->ydpi_values[i]<min)
        {
          min=dev->model->ydpi_values[i];
        }
      i++;
    }
  return min;
}

/** @brief returns the lowest possible dpi for the device
 * Parses device entry to find lowest motor or sensor dpi.
 * @param dev device description
 * @return lowest motor resolution
 */
int sanei_genesys_get_lowest_dpi(Genesys_Device *dev)
{
  int min=20000;
  int i=0;

  while(dev->model->ydpi_values[i]!=0)
    {
      if(dev->model->ydpi_values[i]<min)
        {
          min=dev->model->ydpi_values[i];
        }
      i++;
    }
  i=0;
  while(dev->model->xdpi_values[i]!=0)
    {
      if(dev->model->xdpi_values[i]<min)
        {
          min=dev->model->xdpi_values[i];
        }
      i++;
    }
  return min;
}

/** @brief check is a cache entry may be used
 * Compares current settings with the cache entry and return
 * SANE_TRUE if they are compatible.
 * A calibration cache is compatible if color mode and x dpi match the user
 * requested scan. In the case of CIS scanners, dpi isn't a criteria.
 * flatbed cache entries are considred too old and then expires if they
 * are older than the expiration time option, forcing calibration at least once
 * then given time. */
bool sanei_genesys_is_compatible_calibration(Genesys_Device * dev, const Genesys_Sensor& sensor,
                                             Genesys_Calibration_Cache * cache, int for_overwrite)
{
#ifdef HAVE_SYS_TIME_H
  struct timeval time;
#endif
  int compatible = 1, resolution;

  DBGSTART;

  if(dev->model->cmd_set->calculate_current_setup==NULL)
    {
      DBG (DBG_proc, "%s: no calculate_setup, non compatible cache\n", __func__);
      return false;
    }

    dev->model->cmd_set->calculate_current_setup(dev, sensor);

  DBG (DBG_proc, "%s: checking\n", __func__);

  /* a calibration cache is compatible if color mode and x dpi match the user
   * requested scan. In the case of CIS scanners, dpi isn't a criteria */
  if (dev->model->is_cis == SANE_FALSE)
    {
      resolution = dev->settings.xres;
      if(resolution>sensor.optical_res)
        {
          resolution=sensor.optical_res;
        }
      compatible = (resolution == ((int) cache->used_setup.xres));
    }
  else
    {
      resolution=sanei_genesys_compute_dpihw(dev, sensor, dev->settings.xres);
      compatible = (resolution == ((int) sanei_genesys_compute_dpihw(dev, sensor,cache->used_setup.xres)));
    }
  DBG (DBG_io, "%s: after resolution check current compatible=%d\n", __func__, compatible);
  if (dev->current_setup.ccd_size_divisor != cache->used_setup.ccd_size_divisor)
    {
      DBG (DBG_io, "%s: half_ccd=%d, used=%d\n", __func__,
           dev->current_setup.ccd_size_divisor, cache->used_setup.ccd_size_divisor);
      compatible = 0;
    }
  if (dev->current_setup.params.scan_method != cache->used_setup.params.scan_method)
    {
      DBG (DBG_io, "%s: current method=%d, used=%d\n", __func__,
           static_cast<unsigned>(dev->current_setup.params.scan_method),
           static_cast<unsigned>(cache->used_setup.params.scan_method));
      compatible = 0;
    }
  if (!compatible)
    {
      DBG (DBG_proc, "%s: completed, non compatible cache\n", __func__);
      return false;
    }

  /* a cache entry expires after afetr expiration time for non sheetfed scanners */
  /* this is not taken into account when overwriting cache entries    */
#ifdef HAVE_SYS_TIME_H
  if(for_overwrite == SANE_FALSE && dev->settings.expiration_time >=0)
    {
      gettimeofday (&time, NULL);
      if ((time.tv_sec - cache->last_calibration > dev->settings.expiration_time*60)
          && (dev->model->is_sheetfed == SANE_FALSE)
          && (dev->settings.scan_method == ScanMethod::FLATBED))
        {
          DBG (DBG_proc, "%s: expired entry, non compatible cache\n", __func__);
          return false;
        }
    }
#endif

  DBGCOMPLETED;
  return true;
}


/** @brief compute maximum line distance shift
 * compute maximum line distance shift for the motor and sensor
 * combination. Line distance shift is the distance between different
 * color component of CCD sensors. Since these components aren't at
 * the same physical place, they scan diffrent lines. Software must
 * take this into account to accurately mix color data.
 * @param dev device session to compute max_shift for
 * @param channels number of color channels for the scan
 * @param yres motor resolution used for the scan
 * @param flags scan flags
 * @return 0 or line distance shift
 */
int sanei_genesys_compute_max_shift(Genesys_Device *dev,
                                    int channels,
                                    int yres,
                                    int flags)
{
  int max_shift;

  max_shift=0;
  if (channels > 1 && !(flags & SCAN_FLAG_IGNORE_LINE_DISTANCE))
    {
      max_shift = dev->ld_shift_r;
      if (dev->ld_shift_b > max_shift)
	max_shift = dev->ld_shift_b;
      if (dev->ld_shift_g > max_shift)
	max_shift = dev->ld_shift_g;
      max_shift = (max_shift * yres) / dev->motor.base_ydpi;
    }
  return max_shift;
}

/** @brief build lookup table for digital enhancements
 * Function to build a lookup table (LUT), often
   used by scanners to implement brightness/contrast/gamma
   or by backends to speed binarization/thresholding

   offset and slope inputs are -127 to +127

   slope rotates line around central input/output val,
   0 makes horizontal line

       pos           zero          neg
       .       x     .             .  x
       .      x      .             .   x
   out .     x       .xxxxxxxxxxx  .    x
       .    x        .             .     x
       ....x.......  ............  .......x....
            in            in            in

   offset moves line vertically, and clamps to output range
   0 keeps the line crossing the center of the table

       high           low
       .   xxxxxxxx   .
       . x            .
   out x              .          x
       .              .        x
       ............   xxxxxxxx....
            in             in

   out_min/max provide bounds on output values,
   useful when building thresholding lut.
   0 and 255 are good defaults otherwise.
  * @param lut pointer where to store the generated lut
  * @param in_bits number of bits for in values
  * @param out_bits number of bits of out values
  * @param out_min minimal out value
  * @param out_max maximal out value
  * @param slope slope of the generated data
  * @param offset offset of the generated data
  */
SANE_Status
sanei_genesys_load_lut (unsigned char * lut,
                        int in_bits,
                        int out_bits,
                        int out_min,
                        int out_max,
                        int slope,
                        int offset)
{
  SANE_Status ret = SANE_STATUS_GOOD;
  int i, j;
  double shift, rise;
  int max_in_val = (1 << in_bits) - 1;
  int max_out_val = (1 << out_bits) - 1;
  uint8_t *lut_p8 = lut;
  uint16_t *lut_p16 = (uint16_t *) lut;

  DBGSTART;

  /* slope is converted to rise per unit run:
   * first [-127,127] to [-.999,.999]
   * then to [-PI/4,PI/4] then [0,PI/2]
   * then take the tangent (T.O.A)
   * then multiply by the normal linear slope
   * because the table may not be square, i.e. 1024x256*/
  rise = tan ((double) slope / 128 * M_PI_4 + M_PI_4) * max_out_val / max_in_val;

  /* line must stay vertically centered, so figure
   * out vertical offset at central input value */
  shift = (double) max_out_val / 2 - (rise * max_in_val / 2);

  /* convert the user offset setting to scale of output
   * first [-127,127] to [-1,1]
   * then to [-max_out_val/2,max_out_val/2]*/
  shift += (double) offset / 127 * max_out_val / 2;

  for (i = 0; i <= max_in_val; i++)
    {
      j = rise * i + shift;

      /* cap data to required range */
      if (j < out_min)
	{
	  j = out_min;
	}
      else if (j > out_max)
	{
	  j = out_max;
	}

      /* copy result according to bit depth */
      if (out_bits <= 8)
	{
	  *lut_p8 = j;
	  lut_p8++;
	}
      else
	{
	  *lut_p16 = j;
	  lut_p16++;
	}
    }

  DBGCOMPLETED;
  return ret;
}

void sanei_genesys_usleep(unsigned int useconds)
{
  usleep(useconds);
}

void sanei_genesys_sleep_ms(unsigned int milliseconds)
{
  sanei_genesys_usleep(milliseconds * 1000);
}

static std::unique_ptr<std::vector<std::function<void()>>> s_functions_run_at_backend_exit;

void add_function_to_run_at_backend_exit(std::function<void()> function)
{
    if (!s_functions_run_at_backend_exit)
        s_functions_run_at_backend_exit.reset(new std::vector<std::function<void()>>());
    s_functions_run_at_backend_exit->push_back(std::move(function));
}

void run_functions_at_backend_exit()
{
    for (auto it = s_functions_run_at_backend_exit->rbegin();
         it != s_functions_run_at_backend_exit->rend(); ++it)
    {
        (*it)();
    }
    s_functions_run_at_backend_exit.release();
}

void debug_dump(unsigned level, const Genesys_Settings& settings)
{
    DBG(level, "settings:\n"
        "Resolution X/Y : %u / %u dpi\n"
        "Lines : %u\n"
        "Pixels per line : %u\n"
        "Depth : %u\n"
        "Start position X/Y : %.3f/%.3f\n"
        "Scan mode : %d\n\n",
        settings.xres, settings.yres,
        settings.lines, settings.pixels, settings.depth,
        settings.tl_x, settings.tl_y,
        static_cast<unsigned>(settings.scan_mode));
}

void debug_dump(unsigned level, const SetupParams& params)
{
    DBG(level, "settings:\n"
        "Resolution X/Y : %u / %u dpi\n"
        "Lines : %u\n"
        "Pixels per line : %u\n"
        "Depth : %u\n"
        "Channels : %u\n"
        "Start position X/Y : %g / %g\n"
        "Scan mode : %d\n"
        "Color filter : %d\n"
        "Flags : %x\n",
        params.xres, params.yres,
        params.lines, params.pixels,
        params.depth, params.channels,
        params.startx, params.starty,
        static_cast<unsigned>(params.scan_mode),
        static_cast<unsigned>(params.color_filter),
        params.flags);
}

void debug_dump(unsigned level, const Genesys_Current_Setup& setup)
{
    DBG(level, "current_setup:\n"
        "Pixels: %d\n"
        "Lines: %d\n"
        "Depth: %d\n"
        "Channels: %d\n"
        "exposure_time: %d\n"
        "Resolution X/Y: %g %g\n"
        "ccd_size_divisor: %d\n"
        "stagger: %d\n"
        "max_shift: %d\n",
        setup.pixels,
        setup.lines,
        setup.depth,
        setup.channels,
        setup.exposure_time,
        setup.xres, setup.yres,
        setup.ccd_size_divisor,
        setup.stagger,
        setup.max_shift);
}

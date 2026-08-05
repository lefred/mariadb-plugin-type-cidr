/* Copyright (c) 2019,2024,2025,2026 MariaDB Corporation
   Copyright (c) 2026 lefred (Frédéric Descamps)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA */

#define MYSQL_SERVER
#include "mariadb.h"
#include "sql_class.h"
#include "sql_type_cidr.h"

#include <cstdlib>

/*
  Address parsing/printing below is hand-rolled instead of calling
  inet_pton()/inet_ntop(): those live in <arpa/inet.h>, which is not
  available on Windows. plugin/type_inet solves the same problem the
  same way; the algorithms here are ports of Inet4::ascii_to_fbt() /
  Inet6::ascii_to_fbt() / Inet4::to_string() from that plugin, adapted
  to write into a caller-supplied buffer instead of "m_buffer" (a CIDR
  value's address bytes sit at a different offset than a plain INET4/
  INET6 value's).
*/
static const char HEX_DIGITS[]= "0123456789abcdef";

static void mask_address(uchar *address, uint bits, uint prefix)
{
  for (uint bit= prefix; bit < bits; ++bit)
    address[bit / 8] &= static_cast<uchar>(~(0x80U >> (bit % 8)));
}

/*
  Parse an IPv4 address into CIDR_IN_ADDR_SIZE raw bytes. A portable
  alternative to inet_pton(AF_INET, ...): unlike inet_pton(), it treats
  leading zeros the same way on every platform.
*/
static bool ascii_to_ipv4(const char *str, size_t length, uchar *out)
{
  if (length < 7 || length > CIDR_IN_ADDR_MAX_CHAR_LENGTH)
    return true;

  const char *str_end= str + length;
  const char *p= str;
  int byte_value= 0;
  int chars_in_group= 0;
  int dot_count= 0;
  char c= 0;

  while (p < str_end && *p)
  {
    c= *p++;
    if (my_isdigit(&my_charset_latin1, c))
    {
      if (++chars_in_group > 3)
        return true;
      byte_value= byte_value * 10 + (c - '0');
      if (byte_value > 255)
        return true;
    }
    else if (c == '.')
    {
      if (chars_in_group == 0 || dot_count >= 3)
        return true;
      out[dot_count++]= static_cast<uchar>(byte_value);
      byte_value= 0;
      chars_in_group= 0;
    }
    else
      return true;
  }
  if (c == '.' || dot_count != 3)
    return true;
  out[3]= static_cast<uchar>(byte_value);
  return false;
}

/*
  Parse an IPv6 address (optionally with a trailing embedded IPv4 part,
  e.g. "::ffff:1.2.3.4") into CIDR_IN6_ADDR_SIZE raw bytes. A portable
  alternative to inet_pton(AF_INET6, ...).
*/
static bool ascii_to_ipv6(const char *str, size_t length, uchar *out)
{
  if (length < 2 || length > CIDR_IN6_ADDR_MAX_CHAR_LENGTH)
    return true;

  bzero(out, CIDR_IN6_ADDR_SIZE);

  const char *p= str;
  if (*p == ':')
  {
    ++p;
    if (*p != ':')
      return true;
  }

  const char *str_end= str + length;
  uchar *bytes_end= out + CIDR_IN6_ADDR_SIZE;
  uchar *dst= out;
  uchar *gap_ptr= nullptr;
  const char *group_start_ptr= p;
  int chars_in_group= 0;
  int group_value= 0;

  while (p < str_end && *p)
  {
    char c= *p++;
    if (c == ':')
    {
      group_start_ptr= p;
      if (!chars_in_group)
      {
        if (gap_ptr)
          return true;
        gap_ptr= dst;
        continue;
      }
      if (p >= str_end || !*p)
        return true;
      if (dst + 2 > bytes_end)
        return true;
      dst[0]= static_cast<uchar>((group_value >> 8) & 0xff);
      dst[1]= static_cast<uchar>(group_value & 0xff);
      dst+= 2;
      chars_in_group= 0;
      group_value= 0;
    }
    else if (c == '.')
    {
      /*
        Embedded IPv4 tail, e.g. "::ffff:1.2.3.4". Unlike type_inet's
        Inet6 parser, there is no need to special-case an IPv4 address
        with no ':' at all: ascii_to_fbt() only calls this function once
        it has already seen a ':' somewhere in the input, so a bare
        "1.2.3.4" never reaches here.
      */
      if (dst + CIDR_IN_ADDR_SIZE > bytes_end ||
          ascii_to_ipv4(group_start_ptr,
                        static_cast<size_t>(str_end - group_start_ptr), dst))
        return true;
      dst+= CIDR_IN_ADDR_SIZE;
      chars_in_group= 0;
      break;
    }
    else
    {
      const char *hdp= strchr(HEX_DIGITS, my_tolower(&my_charset_latin1, c));
      if (!hdp || chars_in_group >= 4)
        return true;
      group_value<<= 4;
      group_value|= static_cast<int>(hdp - HEX_DIGITS);
      ++chars_in_group;
    }
  }

  if (chars_in_group > 0)
  {
    if (dst + 2 > bytes_end)
      return true;
    dst[0]= static_cast<uchar>((group_value >> 8) & 0xff);
    dst[1]= static_cast<uchar>(group_value & 0xff);
    dst+= 2;
  }

  if (gap_ptr)
  {
    if (dst == bytes_end)
      return true;
    int bytes_to_move= static_cast<int>(dst - gap_ptr);
    for (int i= 1; i <= bytes_to_move; ++i)
    {
      bytes_end[-i]= gap_ptr[bytes_to_move - i];
      gap_ptr[bytes_to_move - i]= 0;
    }
    dst= bytes_end;
  }

  return dst != bytes_end;
}

size_t cidr_ipv4_to_string(const uchar *address, char *dst, size_t dstsize)
{
  int length= my_snprintf(dst, dstsize, "%d.%d.%d.%d",
                          address[0], address[1], address[2], address[3]);
  return length > 0 ? static_cast<size_t>(length) : 0;
}

/*
  Always prints the full 8-group hex form (with "::" zero-run
  compression); unlike inet_ntop(), it never switches to an embedded
  dotted-decimal tail for IPv4-mapped/-compatible addresses. That keeps
  the worst-case output at CIDR_IN6_ADDR_MAX_CHAR_LENGTH characters,
  matching Cidr_storage::max_char_length().
*/
size_t cidr_ipv6_to_string(const uchar *address, char *dst, size_t dstsize)
{
  struct Region { int pos; int length; };

  uint16 words[CIDR_IN6_ADDR_NUM_WORDS];
  for (size_t i= 0; i < CIDR_IN6_ADDR_NUM_WORDS; ++i)
    words[i]= static_cast<uint16>((address[2 * i] << 8) + address[2 * i + 1]);

  // Find the longest run of zero words, to compress it as "::".
  Region gap= { -1, -1 }, run= { -1, -1 };
  for (size_t i= 0; i < CIDR_IN6_ADDR_NUM_WORDS; ++i)
  {
    if (words[i] != 0)
    {
      if (run.pos >= 0 && run.length > gap.length)
        gap= run;
      run.pos= -1;
      run.length= -1;
    }
    else if (run.pos >= 0)
      ++run.length;
    else
    {
      run.pos= static_cast<int>(i);
      run.length= 1;
    }
  }
  if (run.pos >= 0 && run.length > gap.length)
    gap= run;
  if (gap.length < 2)
    gap.pos= -1; // A run of one zero word prints as "0", not "::".

  char *p= dst;
  char *dstend= dst + dstsize;
  for (int i= 0; i < static_cast<int>(CIDR_IN6_ADDR_NUM_WORDS); ++i)
  {
    if (dstend - p < 5)
      return 0;
    if (i == gap.pos)
    {
      if (i == 0)
        *p++= ':';
      *p++= ':';
      i+= gap.length - 1;
      continue;
    }
    int written= my_snprintf(p, static_cast<size_t>(dstend - p), "%x",
                             words[i]);
    if (written <= 0)
      return 0;
    p+= written;
    /*
      Append a separating ':' after every group except the very last
      one -- including the group immediately before a gap: the gap
      itself contributes the *second* ':' of "::" right after (see
      "i == gap.pos" above), so the two combined produce "::" instead
      of a lone ':'.
    */
    if (i + 1 != static_cast<int>(CIDR_IN6_ADDR_NUM_WORDS))
      *p++= ':';
  }
  *p= '\0';
  return static_cast<size_t>(p - dst);
}

bool Cidr_storage::ascii_to_fbt(const char *str, size_t length)
{
  char input[max_char_length() + 1];
  char *slash;
  char *prefix_text= nullptr;
  uint prefix= 0;
  if (!length || length >= sizeof(input))
    goto error;
  memcpy(input, str, length);
  input[length]= '\0';

  slash= strchr(input, '/');
  if (slash)
  {
    *slash= '\0';
    prefix_text= slash + 1;
    if (!*prefix_text || strchr(prefix_text, '/'))
      goto error;
  }

  bzero(m_buffer, sizeof(m_buffer));
  uint max_prefix;
  uchar *destination;
  if (strchr(input, ':'))
  {
    max_prefix= 128;
    m_buffer[0]= 6;
    destination= reinterpret_cast<uchar *>(m_buffer + 1);
    if (ascii_to_ipv6(input, strlen(input), destination))
      goto error;
  }
  else
  {
    max_prefix= 32;
    m_buffer[0]= 4;
    destination= reinterpret_cast<uchar *>(m_buffer + 13);
    if (ascii_to_ipv4(input, strlen(input), destination))
      goto error;
  }

  prefix= max_prefix;
  if (prefix_text)
  {
    char *end;
    unsigned long parsed= strtoul(prefix_text, &end, 10);
    if (*end || parsed > max_prefix)
      goto error;
    prefix= static_cast<uint>(parsed);
  }
  mask_address(destination, max_prefix, prefix);
  m_buffer[17]= static_cast<char>(prefix);
  return false;

error:
  bzero(m_buffer, sizeof(m_buffer));
  return true;
}

size_t Cidr_storage::to_string(char *dst, size_t dstsize) const
{
  char address_text[CIDR_IN6_ADDR_MAX_CHAR_LENGTH + 1];
  size_t address_length= family() == 4
    ? cidr_ipv4_to_string(address() + 12, address_text, sizeof(address_text))
    : family() == 6
    ? cidr_ipv6_to_string(address(), address_text, sizeof(address_text))
    : 0;
  if (!address_length)
    return 0;
  int length= my_snprintf(dst, dstsize, "%s/%u", address_text,
                          static_cast<uint>(prefix()));
  return length > 0 ? static_cast<size_t>(length) : 0;
}

const Name &Cidr_storage::default_value()
{
  static Name value(STRING_WITH_LEN("0.0.0.0/0"));
  return value;
}

bool cidr_value_from_item(Item *item, Cidr_storage *value, bool warn)
{
  if (item->type_handler() == Type_handler_cidr::singleton())
  {
    NativeBuffer<19> native;
    if (item->val_native(current_thd, &native) || native.length() != 18)
      return true;
    *value= Cidr_storage(native.ptr(), native.length());
    return false;
  }

  StringBuffer<64> buffer;
  String *text= item->val_str(&buffer);
  if (!text)
    return true;
  Type_handler_cidr::Fbt_null parsed(*text);
  if (parsed.is_null())
  {
    if (warn)
      current_thd->push_warning_wrong_value(Sql_condition::WARN_LEVEL_WARN,
        "cidr", ErrConvString(text).ptr());
    return true;
  }
  NativeBuffer<19> native;
  if (parsed.to_fbt().to_native(&native))
    return true;
  *value= Cidr_storage(native.ptr(), native.length());
  return false;
}

bool cidr_value_to_native(const Cidr_storage &value, Native *to)
{
  return to->copy(value.to_lex_cstring().str, Cidr_storage::binary_length());
}

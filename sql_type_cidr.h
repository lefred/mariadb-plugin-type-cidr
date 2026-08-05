#ifndef SQL_TYPE_CIDR_INCLUDED
#define SQL_TYPE_CIDR_INCLUDED
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

#include "sql_type_fixedbin_storage.h"

/*
  Sizes and text-representation bounds for the raw IPv4/IPv6 address
  bytes. These deliberately mirror plugin/type_inet's own constants:
  address parsing/printing is hand-rolled (see sql_type_cidr.cc) instead
  of calling inet_pton()/inet_ntop(), because those live in
  <arpa/inet.h>, which does not exist on Windows.
*/
static const size_t CIDR_IN_ADDR_SIZE= 4;
static const uint CIDR_IN_ADDR_MAX_CHAR_LENGTH= 15;   // "255.255.255.255"

static const size_t CIDR_IN6_ADDR_SIZE= 16;
static const size_t CIDR_IN6_ADDR_NUM_WORDS= CIDR_IN6_ADDR_SIZE / 2;
static const uint CIDR_IN6_ADDR_MAX_CHAR_LENGTH= 8 * 4 + 7; // 8 groups + ':'

class Cidr_storage: public FixedBinTypeStorage<18, 43>
{
public:
  using FixedBinTypeStorage::FixedBinTypeStorage;

  bool ascii_to_fbt(const char *str, size_t length);
  size_t to_string(char *dst, size_t dstsize) const;

  static const Name &default_value();

  uchar family() const { return static_cast<uchar>(m_buffer[0]); }
  uchar prefix() const { return static_cast<uchar>(m_buffer[17]); }
  const uchar *address() const
  { return reinterpret_cast<const uchar *>(m_buffer + 1); }
};

#include "sql_type_fixedbin.h"

typedef Type_handler_fbt<Cidr_storage> Type_handler_cidr;

bool cidr_value_from_item(Item *item, Cidr_storage *value, bool warn= true);
bool cidr_value_to_native(const Cidr_storage &value, Native *to);

/*
  Portable (no <arpa/inet.h>) address-to-text helpers, shared with
  cidr_functions.cc. "address" points at CIDR_IN_ADDR_SIZE (IPv4) or
  CIDR_IN6_ADDR_SIZE (IPv6) raw bytes. Returns the number of characters
  written (excluding the trailing '\0'), or 0 on failure.
*/
size_t cidr_ipv4_to_string(const uchar *address, char *dst, size_t dstsize);
size_t cidr_ipv6_to_string(const uchar *address, char *dst, size_t dstsize);

#endif

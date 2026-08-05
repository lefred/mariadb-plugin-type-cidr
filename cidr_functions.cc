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
#include "item_func.h"
#include "sql_type_cidr.h"
#include "cidr_functions.h"

static uint address_bits(const Cidr_storage &value)
{ return value.family() == 4 ? 32 : 128; }

static uint address_offset(const Cidr_storage &value)
{ return value.family() == 4 ? 12 : 0; }

static bool same_prefix_bits(const Cidr_storage &a, const Cidr_storage &b,
                             uint bits)
{
  if (a.family() != b.family())
    return false;
  const uchar *left= a.address() + address_offset(a);
  const uchar *right= b.address() + address_offset(b);
  const uint bytes= bits / 8;
  if (bytes && memcmp(left, right, bytes))
    return false;
  if (!(bits % 8))
    return true;
  /*
    A partial byte remains to compare. "bits" never exceeds the address
    width (32 for IPv4, 128 for IPv6), so the whole-address case is always
    handled by the "!(bits % 8)" return above, before "bytes" could reach
    16 and index past the 16-byte address region.
  */
  DBUG_ASSERT(bytes < 16);
  return ((left[bytes] ^ right[bytes]) & (0xffU << (8 - bits % 8))) == 0;
}

static bool contains(const Cidr_storage &network, const Cidr_storage &value)
{
  return network.family() == value.family() &&
         network.prefix() <= value.prefix() &&
         same_prefix_bits(network, value, network.prefix());
}

static Cidr_storage make_value(const Cidr_storage &source,
                              const uchar *address, uint prefix)
{
  char native[18];
  bzero(native, sizeof(native));
  native[0]= static_cast<char>(source.family());
  memcpy(native + 1, address, 16);
  native[17]= static_cast<char>(prefix);
  return Cidr_storage(native, sizeof(native));
}

static void endpoint(const Cidr_storage &value, bool last, uchar *output)
{
  memcpy(output, value.address(), 16);
  if (!last)
    return;
  const uint offset= address_offset(value);
  const uint bits= address_bits(value);
  for (uint bit= value.prefix(); bit < bits; ++bit)
    output[offset + bit / 8] |= static_cast<uchar>(0x80U >> (bit % 8));
}

static bool format_address(const Cidr_storage &value, const uchar *address,
                           String *result)
{
  char text[CIDR_IN6_ADDR_MAX_CHAR_LENGTH + 1];
  const uchar *source= address + address_offset(value);
  size_t length= value.family() == 4
    ? cidr_ipv4_to_string(source, text, sizeof(text))
    : cidr_ipv6_to_string(source, text, sizeof(text));
  if (!length)
    return true;
  return result->copy(text, length, &my_charset_latin1);
}

enum Bool_operation { OP_CONTAINS, OP_CONTAINED_BY, OP_OVERLAPS, OP_ADJACENT };

class Item_func_cidr_bool: public Item_bool_func
{
  Bool_operation operation;
  LEX_CSTRING function_name;
public:
  Item_func_cidr_bool(THD *thd, Item *a, Item *b, Bool_operation op,
                      LEX_CSTRING name)
    : Item_bool_func(thd, a, b), operation(op), function_name(name) {}

  bool val_bool() override
  {
    Cidr_storage a, b;
    if (cidr_value_from_item(args[0], &a) ||
        cidr_value_from_item(args[1], &b))
    {
      null_value= true;
      return false;
    }
    null_value= false;
    switch (operation)
    {
    case OP_CONTAINS: return contains(a, b);
    case OP_CONTAINED_BY: return contains(b, a);
    case OP_OVERLAPS: return contains(a, b) || contains(b, a);
    case OP_ADJACENT:
      if (a.family() != b.family() || a.prefix() != b.prefix() ||
          a.prefix() == 0)
        return false;
      return same_prefix_bits(a, b, a.prefix() - 1) &&
             !same_prefix_bits(a, b, a.prefix());
    }
    return false;
  }
  LEX_CSTRING func_name_cstring() const override { return function_name; }
  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Item_func_cidr_bool>(thd, this); }
};

enum Integer_operation { OP_PREFIX, OP_FAMILY };

class Item_func_cidr_integer: public Item_int_func
{
  Integer_operation operation;
  LEX_CSTRING function_name;
public:
  Item_func_cidr_integer(THD *thd, Item *arg, Integer_operation op,
                         LEX_CSTRING name)
    : Item_int_func(thd, arg), operation(op), function_name(name) {}
  longlong val_int() override
  {
    Cidr_storage value;
    if (cidr_value_from_item(args[0], &value))
    {
      null_value= true;
      return 0;
    }
    null_value= false;
    return operation == OP_PREFIX ? value.prefix() : value.family();
  }
  const Type_handler *type_handler() const override
  { return &type_handler_slonglong; }
  LEX_CSTRING func_name_cstring() const override { return function_name; }
  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Item_func_cidr_integer>(thd, this); }
};

enum Address_operation { OP_NETMASK, OP_HOSTMASK, OP_BROADCAST,
                         OP_FIRST, OP_LAST, OP_COUNT };

class Item_func_cidr_string: public Item_str_func
{
  Address_operation operation;
  LEX_CSTRING function_name;
public:
  Item_func_cidr_string(THD *thd, Item *arg, Address_operation op,
                        LEX_CSTRING name)
    : Item_str_func(thd, arg), operation(op), function_name(name) {}
  bool fix_length_and_dec(THD *) override
  {
    collation.set(&my_charset_latin1);
    max_length= operation == OP_COUNT ? 39 : CIDR_IN6_ADDR_MAX_CHAR_LENGTH;
    set_maybe_null();
    return false;
  }
  String *val_str(String *result) override
  {
    Cidr_storage value;
    if (cidr_value_from_item(args[0], &value))
    {
      null_value= true;
      return nullptr;
    }
    if (operation == OP_COUNT)
    {
      /*
        Compute 2^(address_bits - prefix) as a decimal string by repeated
        doubling. The digits are kept least-significant-first at the end
        of a fixed buffer, growing towards its start; 40 bytes is enough
        for the largest possible value (2^128 has 39 decimal digits).
      */
      char digits[40];
      uint length= 1;
      digits[sizeof(digits) - 1]= '1';
      for (uint i= value.prefix(); i < address_bits(value); ++i)
      {
        uint carry= 0;
        for (uint n= 0; n < length; ++n)
        {
          char *d= &digits[sizeof(digits) - 1 - n];
          uint digit= static_cast<uint>(*d - '0') * 2 + carry;
          *d= static_cast<char>('0' + digit % 10);
          carry= digit / 10;
        }
        if (carry)
          digits[sizeof(digits) - 1 - length++]=
            static_cast<char>('0' + carry);
      }
      null_value= result->copy(digits + sizeof(digits) - length, length,
                               &my_charset_latin1);
      return null_value ? nullptr : result;
    }

    uchar address[16];
    if (operation == OP_NETMASK || operation == OP_HOSTMASK)
    {
      bzero(address, sizeof(address));
      const uint offset= address_offset(value);
      for (uint bit= 0; bit < address_bits(value); ++bit)
        if ((bit < value.prefix()) == (operation == OP_NETMASK))
          address[offset + bit / 8] |= static_cast<uchar>(0x80U >> (bit % 8));
    }
    else
      endpoint(value, operation == OP_BROADCAST || operation == OP_LAST,
               address);
    null_value= format_address(value, address, result);
    return null_value ? nullptr : result;
  }
  LEX_CSTRING func_name_cstring() const override { return function_name; }
  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Item_func_cidr_string>(thd, this); }
};

enum Cidr_operation { OP_NETWORK, OP_SUPERNET, OP_SUBNET, OP_MERGE };

class Item_func_cidr_result: public Type_handler_cidr::Item_fbt_func
{
  Cidr_operation operation;
  LEX_CSTRING function_name;

  bool calculate(Cidr_storage *result)
  {
    Cidr_storage a;
    if (cidr_value_from_item(args[0], &a))
      return true;
    if (operation == OP_NETWORK)
    {
      *result= a;
      return false;
    }
    if (operation == OP_SUPERNET)
    {
      longlong prefix= a.prefix() ? a.prefix() - 1 : -1;
      if (arg_count == 2)
      {
        prefix= args[1]->val_int();
        if (args[1]->null_value)
          return true;
      }
      if (prefix < 0 || prefix >= a.prefix())
        return true;
      uchar address[16];
      memcpy(address, a.address(), sizeof(address));
      const uint offset= address_offset(a);
      for (uint bit= static_cast<uint>(prefix); bit < address_bits(a); ++bit)
        address[offset + bit / 8] &= static_cast<uchar>(~(0x80U >> (bit % 8)));
      *result= make_value(a, address, static_cast<uint>(prefix));
      return false;
    }
    if (operation == OP_SUBNET)
    {
      longlong new_prefix= args[1]->val_int();
      longlong signed_number= args[2]->val_int();
      ulonglong number= static_cast<ulonglong>(signed_number);
      if (args[1]->null_value || args[2]->null_value ||
          new_prefix < a.prefix() || new_prefix > address_bits(a) ||
          (!args[2]->unsigned_flag && signed_number < 0))
        return true;
      uint extra= static_cast<uint>(new_prefix) - a.prefix();
      if (extra < 64 && number >= (1ULL << extra))
        return true;
      if (extra == 0 && number)
        return true;
      uchar address[16];
      memcpy(address, a.address(), sizeof(address));
      for (uint i= 0; i < extra; ++i)
      {
        uint source_bit= extra - i - 1;
        if (source_bit < 64 && ((number >> source_bit) & 1))
        {
          uint bit= a.prefix() + i;
          address[address_offset(a) + bit / 8] |=
            static_cast<uchar>(0x80U >> (bit % 8));
        }
      }
      *result= make_value(a, address, static_cast<uint>(new_prefix));
      return false;
    }

    Cidr_storage b;
    if (cidr_value_from_item(args[1], &b) || a.family() != b.family())
      return true;
    uint common= 0;
    uint limit= MY_MIN(static_cast<uint>(a.prefix()),
                       static_cast<uint>(b.prefix()));
    while (common < limit && same_prefix_bits(a, b, common + 1))
      ++common;
    uchar address[16];
    memcpy(address, a.address(), sizeof(address));
    const uint offset= address_offset(a);
    for (uint bit= common; bit < address_bits(a); ++bit)
      address[offset + bit / 8] &= static_cast<uchar>(~(0x80U >> (bit % 8)));
    *result= make_value(a, address, common);
    return false;
  }

public:
  Item_func_cidr_result(THD *thd, Item *a, Cidr_operation op,
                        LEX_CSTRING name)
    : Item_fbt_func(thd, a), operation(op), function_name(name) {}
  Item_func_cidr_result(THD *thd, Item *a, Item *b, Cidr_operation op,
                        LEX_CSTRING name)
    : Item_fbt_func(thd, a, b), operation(op), function_name(name) {}
  Item_func_cidr_result(THD *thd, Item *a, Item *b, Item *c,
                        Cidr_operation op, LEX_CSTRING name)
    : Item_fbt_func(thd, a, b, c), operation(op), function_name(name) {}
  bool fix_length_and_dec(THD *thd) override
  {
    Item_fbt_func::fix_length_and_dec(thd);
    set_maybe_null();
    return false;
  }
  bool val_native(THD *, Native *to) override
  {
    Cidr_storage value;
    return null_value= calculate(&value) || cidr_value_to_native(value, to);
  }
  String *val_str(String *to) override
  {
    Cidr_storage value;
    if ((null_value= calculate(&value)))
      return nullptr;
    char text[44];
    size_t length= value.to_string(text, sizeof(text));
    if (!length || to->copy(text, length, &my_charset_latin1))
    {
      null_value= true;
      return nullptr;
    }
    return to;
  }
  LEX_CSTRING func_name_cstring() const override { return function_name; }
  Item *shallow_copy(THD *thd) const override
  { return get_item_copy<Item_func_cidr_result>(thd, this); }
};

class Create_cidr_bool: public Create_func_arg2
{
  Bool_operation operation; LEX_CSTRING name;
public:
  Create_cidr_bool(Bool_operation op, LEX_CSTRING n): operation(op), name(n) {}
  Item *create_2_arg(THD *thd, Item *a, Item *b) override
  {
    return new (thd->mem_root)
      Item_func_cidr_bool(thd, a, b, operation, name);
  }
};

class Create_cidr_integer: public Create_func_arg1
{
  Integer_operation operation; LEX_CSTRING name;
public:
  Create_cidr_integer(Integer_operation op, LEX_CSTRING n)
    : operation(op), name(n) {}
  Item *create_1_arg(THD *thd, Item *a) override
  {
    return new (thd->mem_root)
      Item_func_cidr_integer(thd, a, operation, name);
  }
};

class Create_cidr_string: public Create_func_arg1
{
  Address_operation operation; LEX_CSTRING name;
public:
  Create_cidr_string(Address_operation op, LEX_CSTRING n)
    : operation(op), name(n) {}
  Item *create_1_arg(THD *thd, Item *a) override
  {
    return new (thd->mem_root)
      Item_func_cidr_string(thd, a, operation, name);
  }
};

class Create_cidr_network: public Create_func_arg1
{
public: Item *create_1_arg(THD *thd, Item *a) override
  { return new (thd->mem_root) Item_func_cidr_result(thd, a, OP_NETWORK,
      "cidr_network"_LEX_CSTRING); }
};
class Create_cidr_supernet: public Create_native_func
{
public:
  Item *create_native(THD *thd, const LEX_CSTRING *,
                      List<Item> *list) override
  {
    if (!list || (list->elements != 1 && list->elements != 2))
      return nullptr;
    Item_args args(thd, *list);
    if (list->elements == 1)
      return new (thd->mem_root)
        Item_func_cidr_result(thd, args.arguments()[0], OP_SUPERNET,
                              "cidr_supernet"_LEX_CSTRING);
    return new (thd->mem_root)
      Item_func_cidr_result(thd, args.arguments()[0], args.arguments()[1],
                            OP_SUPERNET, "cidr_supernet"_LEX_CSTRING);
  }
};
class Create_cidr_subnet: public Create_func_arg3
{
public: Item *create_3_arg(THD *thd, Item *a, Item *b, Item *c) override
  { return new (thd->mem_root) Item_func_cidr_result(thd, a, b, c, OP_SUBNET,
      "cidr_subnet"_LEX_CSTRING); }
};
class Create_cidr_merge: public Create_func_arg2
{
public: Item *create_2_arg(THD *thd, Item *a, Item *b) override
  { return new (thd->mem_root) Item_func_cidr_result(thd, a, b, OP_MERGE,
      "cidr_merge"_LEX_CSTRING); }
};

static Create_cidr_bool create_contains(OP_CONTAINS,
                                        "cidr_contains"_LEX_CSTRING);
static Create_cidr_bool create_contained(OP_CONTAINED_BY,
                                         "cidr_contained_by"_LEX_CSTRING);
static Create_cidr_bool create_overlaps(OP_OVERLAPS,
                                        "cidr_overlaps"_LEX_CSTRING);
static Create_cidr_bool create_adjacent(OP_ADJACENT,
                                        "cidr_adjacent"_LEX_CSTRING);
static Create_cidr_integer create_prefix(OP_PREFIX,
                                         "cidr_prefix_length"_LEX_CSTRING);
static Create_cidr_integer create_family(OP_FAMILY,
                                         "cidr_family"_LEX_CSTRING);
static Create_cidr_string create_netmask(OP_NETMASK,
                                         "cidr_netmask"_LEX_CSTRING);
static Create_cidr_string create_hostmask(OP_HOSTMASK,
                                          "cidr_hostmask"_LEX_CSTRING);
static Create_cidr_string create_broadcast(OP_BROADCAST,
                                           "cidr_broadcast"_LEX_CSTRING);
static Create_cidr_string create_first(OP_FIRST,
                                       "cidr_first_address"_LEX_CSTRING);
static Create_cidr_string create_last(OP_LAST,
                                      "cidr_last_address"_LEX_CSTRING);
static Create_cidr_string create_count(OP_COUNT,
                                       "cidr_address_count"_LEX_CSTRING);
static Create_cidr_network create_network;
static Create_cidr_supernet create_supernet;
static Create_cidr_subnet create_subnet;
static Create_cidr_merge create_merge;

Plugin_function plugin_descriptor_cidr_contains(&create_contains);
Plugin_function plugin_descriptor_cidr_contained_by(&create_contained);
Plugin_function plugin_descriptor_cidr_overlaps(&create_overlaps);
Plugin_function plugin_descriptor_cidr_network(&create_network);
Plugin_function plugin_descriptor_cidr_netmask(&create_netmask);
Plugin_function plugin_descriptor_cidr_hostmask(&create_hostmask);
Plugin_function plugin_descriptor_cidr_broadcast(&create_broadcast);
Plugin_function plugin_descriptor_cidr_prefix_length(&create_prefix);
Plugin_function plugin_descriptor_cidr_family(&create_family);
Plugin_function plugin_descriptor_cidr_first_address(&create_first);
Plugin_function plugin_descriptor_cidr_last_address(&create_last);
Plugin_function plugin_descriptor_cidr_address_count(&create_count);
Plugin_function plugin_descriptor_cidr_supernet(&create_supernet);
Plugin_function plugin_descriptor_cidr_subnet(&create_subnet);
Plugin_function plugin_descriptor_cidr_adjacent(&create_adjacent);
Plugin_function plugin_descriptor_cidr_merge(&create_merge);

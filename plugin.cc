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

/* MariaDB CIDR data type and companion functions. */
#define MYSQL_SERVER
#include "mariadb.h"
#include "sql_class.h"
#include "sql_type_cidr.h"
#include "cidr_functions.h"
#include <mysql/plugin_data_type.h>

static struct st_mariadb_data_type plugin_descriptor_cidr=
{
  MariaDB_DATA_TYPE_INTERFACE_VERSION,
  Type_handler_cidr::singleton()
};

#define CIDR_PLUGIN_ENTRY(TYPE, DESCRIPTOR, NAME, DESCRIPTION) \
{ TYPE, DESCRIPTOR, NAME, "lefred", DESCRIPTION, PLUGIN_LICENSE_GPL, \
  0, 0, 0x0100, NULL, NULL, "1.0.0", MariaDB_PLUGIN_MATURITY_BETA }

maria_declare_plugin(type_cidr)
  CIDR_PLUGIN_ENTRY(MariaDB_DATA_TYPE_PLUGIN, &plugin_descriptor_cidr,
                    "cidr", "Fixed-size IPv4 and IPv6 CIDR data type"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_contains,
                    "cidr_contains", "Test whether a CIDR contains another"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN,
                    &plugin_descriptor_cidr_contained_by, "cidr_contained_by",
                    "Test whether a CIDR is contained by another"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_overlaps,
                    "cidr_overlaps", "Test whether two CIDRs overlap"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_network,
                    "cidr_network", "Return the canonical network"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_netmask,
                    "cidr_netmask", "Return the network mask"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_hostmask,
                    "cidr_hostmask", "Return the host mask"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_broadcast,
                    "cidr_broadcast", "Return the last network address"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN,
                    &plugin_descriptor_cidr_prefix_length, "cidr_prefix_length",
                    "Return the prefix length"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_family,
                    "cidr_family", "Return 4 for IPv4 or 6 for IPv6"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN,
                    &plugin_descriptor_cidr_first_address, "cidr_first_address",
                    "Return the first network address"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN,
                    &plugin_descriptor_cidr_last_address, "cidr_last_address",
                    "Return the last network address"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN,
                    &plugin_descriptor_cidr_address_count, "cidr_address_count",
                    "Return the number of addresses"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_supernet,
                    "cidr_supernet", "Return a containing supernet"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_subnet,
                    "cidr_subnet", "Return a numbered subnet"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_adjacent,
                    "cidr_adjacent", "Test whether two CIDRs are adjacent"),
  CIDR_PLUGIN_ENTRY(MariaDB_FUNCTION_PLUGIN, &plugin_descriptor_cidr_merge,
                    "cidr_merge", "Return the smallest common supernet")
maria_declare_plugin_end;

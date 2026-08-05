#ifndef CIDR_FUNCTIONS_INCLUDED
#define CIDR_FUNCTIONS_INCLUDED
/* Copyright (c) 2019,2024,2025,2026 MariaDB Corporation
   Copyright (c) 2026 lefred (Frédéric Descamps)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <mysql/plugin_function.h>

extern Plugin_function plugin_descriptor_cidr_contains;
extern Plugin_function plugin_descriptor_cidr_contained_by;
extern Plugin_function plugin_descriptor_cidr_overlaps;
extern Plugin_function plugin_descriptor_cidr_network;
extern Plugin_function plugin_descriptor_cidr_netmask;
extern Plugin_function plugin_descriptor_cidr_hostmask;
extern Plugin_function plugin_descriptor_cidr_broadcast;
extern Plugin_function plugin_descriptor_cidr_prefix_length;
extern Plugin_function plugin_descriptor_cidr_family;
extern Plugin_function plugin_descriptor_cidr_first_address;
extern Plugin_function plugin_descriptor_cidr_last_address;
extern Plugin_function plugin_descriptor_cidr_address_count;
extern Plugin_function plugin_descriptor_cidr_supernet;
extern Plugin_function plugin_descriptor_cidr_subnet;
extern Plugin_function plugin_descriptor_cidr_adjacent;
extern Plugin_function plugin_descriptor_cidr_merge;

#endif

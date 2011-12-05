/** @file

  Support class for describing the local machine.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "libts.h"
#include "I_Machine.h"

# if TS_HAVE_IFADDRS_H
#include <ifaddrs.h>
# else
# endif

// Singleton
Machine* Machine::_instance = NULL;

Machine*
Machine::instance() {
  ink_assert(_instance || !"Machine instance accessed before initialization");
  return Machine::_instance;
}

Machine*
Machine::init(char const* name, sockaddr const* ip) {
  ink_assert(!_instance || !"Machine instance initialized twice.");
  Machine::_instance = new Machine(name, ip);
  return Machine::_instance;
}

Machine::Machine(char const* the_hostname, sockaddr const* addr)
  : hostname(0), hostname_len(0)
  , ip_string_len(0)
  , ip_hex_string_len(0)
{
  char localhost[1024];
  int status; // return for system calls.

  ip_string[0] = 0;
  ip_hex_string[0] = 0;
  ink_zero(ip);
  ink_zero(ip4);
  ink_zero(ip6);

  localhost[sizeof(localhost)-1] = 0; // ensure termination.

  if (!ink_inet_is_ip(addr)) {
    if (!the_hostname) {
      ink_release_assert(!gethostname(localhost, sizeof(localhost)-1));
      the_hostname = localhost;
    }
    hostname = ats_strdup(the_hostname);

#   if TS_HAVE_IFADDRS_H
      ifaddrs* ifa_addrs = 0;
      status = getifaddrs(&ifa_addrs);
#   else
      int s = socket(AF_INET, SOCK_DGRAM, 0);
      // This number is hard to determine, but needs to be much larger than
      // you would expect. On a normal system with just two interfaces and
      // one address / interface the return count is 120. Stack space is
      // cheap so it's best to go big.
      static const int N_REQ = 1024;
      ifconf conf;
      ifreq req[N_REQ];
      if (0 <= s) {
        conf.ifc_len = sizeof(req);
        conf.ifc_req = req;
        status = ioctl(s, SIOCGIFCONF, &conf);
        close(s);
      } else {
        status = -1;
      }
#   endif

    if (0 != status) {
      Warning("Unable to determine local host '%s' address information - %s"
        , hostname
        , strerror(errno)
      );
    } else {
      // Loop through the interface addresses and prefer by type.
      enum {
        NA, // Not an (IP) Address.
        LO, // Loopback.
        NR, // Non-Routable.
        MC, // Multicast.
        GA  // Globally unique Address.
      } spot_type = NA, ip4_type = NA, ip6_type = NA;
      sockaddr const* ifip;
      for (
#     if TS_HAVE_IFADDRS_H
        ifaddrs* spot = ifa_addrs ; spot ; spot = spot->ifa_next
#     else
          ifreq* spot = req, *req_limit = req + (conf.ifc_len/sizeof(*req)) ; spot < req_limit ; ++spot
#     endif
      ) {
#     if TS_HAVE_IFADDRS_H
        ifip = spot->ifa_addr;
#     else
        ifip = &spot->ifr_addr;
#     endif

        if (!ink_inet_is_ip(ifip)) spot_type = NA;
        else if (ink_inet_is_loopback(ifip)) spot_type = LO;
        else if (ink_inet_is_nonroutable(ifip)) spot_type = NR;
        else if (ink_inet_is_multicast(ifip)) spot_type = MC;
        else spot_type = GA;

        if (spot_type == NA) continue; // Next!

        if (ink_inet_is_ip4(ifip)) {
          if (spot_type > ip4_type) {
            ink_inet_copy(&ip4, ifip);
            ip4_type = spot_type;
          }
        } else if (ink_inet_is_ip6(ifip)) {
          if (spot_type > ip6_type) {
            ink_inet_copy(&ip6, ifip);
            ip6_type = spot_type;
          }
        }
      }

#     if TS_HAVE_IFADDRS_H
      freeifaddrs(ifa_addrs);
#     endif

      // What about the general address? Prefer IPv4?
      if (ip4_type >= ip6_type)
        ink_inet_copy(&ip.sa, &ip4.sa);
      else
        ink_inet_copy(&ip.sa, &ip6.sa);
    }
  } else { // address provided.
    ink_inet_copy(&ip, addr);
    if (ink_inet_is_ip4(addr)) ink_inet_copy(&ip4, addr);
    else if (ink_inet_is_ip6(addr)) ink_inet_copy(&ip6, addr);

    status = getnameinfo(
      addr, ink_inet_ip_size(addr),
      localhost, sizeof(localhost) - 1,
      0, 0, // do not request service info
      0 // no flags.
    );

    if (0 != status) {
      ip_text_buffer ipbuff;
      Warning("Failed to find hostname for address '%s' - %s"
        , ink_inet_ntop(addr, ipbuff, sizeof(ipbuff))
        , gai_strerror(status)
      );
    } else
      hostname = ats_strdup(localhost);
  }

  hostname_len = hostname ? strlen(hostname) : 0;

  ink_inet_ntop(&ip.sa, ip_string, sizeof(ip_string));
  ip_string_len = strlen(ip_string);
  ip_hex_string_len = ink_inet_to_hex(&ip.sa, ip_hex_string, sizeof(ip_hex_string));
}

Machine::~Machine()
{
  ats_free(hostname);
}

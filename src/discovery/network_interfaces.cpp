#include "nockvm/discovery/network_interfaces.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace nockvm::discovery {

#ifdef _WIN32

std::vector<uint32_t> get_broadcast_targets() {
  std::vector<uint32_t> out;
  ULONG buf_len = 15000;
  std::vector<uint8_t> buffer(buf_len);
  auto* addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
  const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

  DWORD result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buf_len);
  if (result == ERROR_BUFFER_OVERFLOW) {
    buffer.resize(buf_len);
    addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buf_len);
  }
  if (result != NO_ERROR) return out;

  for (auto* adapter = addresses; adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp) continue;
    if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* ua = adapter->FirstUnicastAddress; ua; ua = ua->Next) {
      if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
      auto* addr_in = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
      const uint32_t ip_host = ntohl(addr_in->sin_addr.s_addr);
      const uint8_t prefix_len = ua->OnLinkPrefixLength;
      const uint32_t mask_host = prefix_len == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix_len));
      const uint32_t broadcast_host = (ip_host & mask_host) | ~mask_host;
      out.push_back(htonl(broadcast_host));
    }
  }
  return out;
}

#else

std::vector<uint32_t> get_broadcast_targets() {
  std::vector<uint32_t> out;
  ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) != 0) return out;

  for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
    if (ifa->ifa_flags & IFF_LOOPBACK) continue;
    if (!(ifa->ifa_flags & IFF_BROADCAST) || !ifa->ifa_broadaddr) continue;
    auto* broadcast_addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
    out.push_back(broadcast_addr->sin_addr.s_addr);
  }

  freeifaddrs(ifaddr);
  return out;
}

#endif

}  // namespace nockvm::discovery

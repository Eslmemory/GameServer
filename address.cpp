#include "address.h"
#include "endian.h"
#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>

namespace WebServer {

	template<typename T>
	static T CreateMask(uint32_t bits) {
		return (1 << (sizeof(T) * 8 - bits)) - 1;
	}

	// 计算二进制数中1的个数
	template<typename T>
	static uint32_t CountBytes(T value) {
		uint32_t result = 0;
		for (; value; ++result)
			value &= value - 1;
		return result;
	}

	/*
		ai_flags           控制地址查找的标志
		ai_family          地址族，如 AF_INET（IPv4）或 AF_INET6（IPv6）
		ai_socktype        套接字类型，如 SOCK_STREAM（流式套接字）或 SOCK_DGRAM（数据报套接字）
		ai_protocol        协议，一般为 0 表示任意协议
		ai_addrlen         地址长度
		ai_canonname       主机的规范名字（如果有）
		sockaddr* ai_addr  指向 socket 地址的指针
		ai_next;           指向链表中下一个 addrinfo 结构的指针
	*/
	bool Address::Lookup(std::vector<Address::addressPtr>& result, const std::string& host, int family, int type, int protocol) {
		addrinfo hints, *results, *next;
		hints.ai_flags = 0;
		hints.ai_family = family;
		hints.ai_socktype = type;
		hints.ai_protocol = protocol;
		hints.ai_addrlen = 0;
		hints.ai_canonname = nullptr;
		hints.ai_addr = nullptr;
		hints.ai_next = nullptr;

		std::string node;
		const char* service = nullptr;

		// example: "[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:8080"
		if (!host.empty() && host[0] == '[') {
			const char* endipv6 = (const char*)memchr(host.c_str() + 1, ']', host.size() - 1);
			if (endipv6) {
				if (*(endipv6 + 1) == ':')
					service = endipv6 + 2;
				node = host.substr(1, endipv6 - host.c_str() - 1);
			}
		}

		// example: "baidu.com:8080"
		if (node.empty()) {
			service = (const char*)memchr(host.c_str(), ':', host.size());
			if (service) {
				if (!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)) {
					node = host.substr(0, service - host.c_str());
					++service;
				}
			}
		}

		if (node.empty()) {
			node = host;
		}

		int error = getaddrinfo(node.c_str(), service, &hints, &results);
		if (error) {
			std::cout << "Address::Lookup getaddress(" << host << "," << family << "," << type << ") error="
				<< error << " errstr=" << gai_strerror(error) << std::endl;
			return false;
		}

		next = results;
		while (next) {
			result.push_back(Create(next->ai_addr, (socklen_t)next->ai_addrlen));
			next = next->ai_next;
		}

		freeaddrinfo(results);
		return !result.empty();
	}

	Address::addressPtr Address::LookupAny(const std::string& host, int family, int type, int protocol) {
		std::vector<Address::addressPtr> result;
		if (Lookup(result, host, family, type, protocol))
			return result[0];
		return nullptr;
	}

	IPAddress::ipAddressPtr Address::LookupAnyIpAddress(const std::string& host, int family, int type, int protocol) {
		std::vector<Address::addressPtr> result;
		if (Lookup(result, host, family, type, protocol)) {
			for (auto& i : result) {
				IPAddress::ipAddressPtr v = std::dynamic_pointer_cast<IPAddress>(i);
				if (v) {
					return v;
				}
			}
		}
		return nullptr;
	}

	/*
		ifa_next     指向链表中下一个元素的指针
		ifa_name     接口名
		ifa_addr     接口地址
		ifa_netmask  掩码
		ifa_dstaddr  目标地址
		ifa_data     指向接口私有数据的指针
	*/
	bool Address::GetInterfaceAddress(std::multimap<std::string, std::pair<Address::addressPtr, uint32_t>>& result, int family) {
		struct ifaddrs* next, *results;
		if (getifaddrs(&results) != 0) {
			std::cout << "Address::GetInterfaceAddresses getifaddrs" << " error=" << errno << " errstr=" << strerror(errno);
			return false;
		}

		try {
			for (next = results; next; next = next->ifa_next) {
				Address::addressPtr addr;
				uint32_t prefixLen = ~0u;
				if (family != AF_UNSPEC && family != next->ifa_addr->sa_family)
					continue;

				switch (next->ifa_addr->sa_family) {
				case AF_INET:
					{
					addr = Create(next->ifa_addr, sizeof(sockaddr_in));
					uint32_t netmask = ((sockaddr_in*)next->ifa_netmask)->sin_addr.s_addr;
					prefixLen = CountBytes(netmask);
					}
					break;
				case AF_INET6:
					{
					addr = Create(next->ifa_addr, sizeof(sockaddr_in6));
					in6_addr& netmask = ((sockaddr_in6*)next->ifa_netmask)->sin6_addr;
					prefixLen = 0;
					for (int i = 0; i < 16; i++)
						prefixLen += CountBytes(netmask.s6_addr[i]);
					}
					break;
				default:
					break;
				}
				if (addr)
					result.insert(std::make_pair(next->ifa_name, std::make_pair(addr, prefixLen)));
			}
		}
		catch(...) {
			printf("[Error] Address:GetInterfaceAddresses exception");
			freeifaddrs(results);
			return false;
		}
		freeifaddrs(results);
		return !result.empty();
	}

	bool Address::GetInterfaceAddress(std::vector<std::pair<Address::addressPtr, uint32_t>>& result,
		const std::string& iface, int family) {
		if (iface.empty() || iface == "*") {
			if (family == AF_INET || family == AF_UNSPEC)
				result.push_back(std::make_pair(Address::addressPtr(new IPv4Address()), 0u));
			// TODO: IPv6
			// if (family == AF_INET6 || family == AF_UNSPEC)
			// 	result.push_back(std::make_pair(Address::addressPtr(new IPv6Address()), 0u));
			return true;
		}

		std::multimap<std::string, std::pair<Address::addressPtr, uint32_t>> results;
		if (!GetInterfaceAddress(results, family))
			return false;
		auto its = results.equal_range(iface);
		for (; its.first != its.second; ++its.first)
			result.push_back(its.first->second);
		return !result.empty();
	}

	int Address::getFamily() const {
		return getAddr()->sa_family;
	}

	std::string Address::toString() const {
		std::stringstream ss;
		insert(ss);
		return ss.str();
	}

	bool Address::operator<(const Address& rhs) const {
		socklen_t minLen = std::min(getAddrLen(), rhs.getAddrLen());
		int result = memcmp(getAddr(), rhs.getAddr(), minLen);
		if (result < 0)
			return true;
		else if (result > 0)
			return false;
		else if (getAddrLen() < rhs.getAddrLen())
			return true;
		return false;
	}

	bool Address::operator==(const Address& rhs) const {
		return getAddrLen() == rhs.getAddrLen() && memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
	}

	bool Address::operator!=(const Address& rhs) const {
		return !(*this == rhs);
	}

	Address::addressPtr Address::Create(const sockaddr* addr, socklen_t addrlen) {
		if (addr == nullptr)
			return nullptr;

		Address::addressPtr result;
		switch (addr->sa_family) {
		case AF_INET:
			result.reset(new IPv4Address(*(const sockaddr_in*)addr));
			break;
		case AF_INET6:
			// TODO: IPv6
			// result.reset(new IPv6Address(*(const sockaddr_in6*)addr));
			break;
		default:
			// TODO: Unknow
			// result.reset(new UnknowAddress(*addr));
			break;
		}
		return result;
	}

	IPAddress::ipAddressPtr IPAddress::Create(const char* address, uint16_t port) {
		addrinfo hints, *results;
		memset(&hints, 0, sizeof(addrinfo));
		
		hints.ai_flags = AI_NUMERICHOST;
		hints.ai_family = AF_UNSPEC;

		int error = getaddrinfo(address, nullptr, &hints, &results);
		if (error) {
			std::cout << "IPAddress::Create(" << address
				<< ", " << port << ") error=" << error
				<< " errno=" << errno << " errstr=" << strerror(errno);
			return nullptr;
		}

		try {
			IPAddress::ipAddressPtr result = std::dynamic_pointer_cast<IPAddress>(Address::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
			if (result)
				result->setPort(port);
			freeaddrinfo(results);
			return result;
		}
		catch (...) {
			freeaddrinfo(results);
			return nullptr;
		}
	}


	IPv4Address::ipv4AddressPtr IPv4Address::Create(const char* address, uint16_t port) {
		IPv4Address::ipv4AddressPtr ipv4Addr(new IPv4Address);
		ipv4Addr->m_Addr.sin_port = byteswapOnLittleEndian(port);
		int result = inet_pton(AF_INET, address, &ipv4Addr->m_Addr.sin_addr);
		if (result <= 0) {
			std::cout << "IPv4Address::Create(" << address << ", "
				<< port << ") rt=" << result << " errno=" << errno
				<< " errstr=" << strerror(errno);
			return nullptr;
		}
		return ipv4Addr;
	}

	IPv4Address::IPv4Address(const sockaddr_in& address) {
		m_Addr = address;
	}

	IPv4Address::IPv4Address(uint32_t address, uint16_t port) {
		memset(&m_Addr, 0, sizeof(m_Addr));
		m_Addr.sin_family = AF_INET;
		// 网络协议规定了大端存储,因此使用大端
		// 这里byteswapOnLittleEndian的意思是我现在的数据所在的机子是小端存储
		// 如果WS_BYTE_ORDER要求是大端,就会byteswap,如果要求的是小端就不会
		m_Addr.sin_port = byteswapOnLittleEndian(port);
		m_Addr.sin_addr.s_addr = byteswapOnLittleEndian(address);
	}

	const sockaddr* IPv4Address::getAddr() const {
		return (sockaddr*)&m_Addr;
	}

	sockaddr* IPv4Address::getAddr() {
		return (sockaddr*)&m_Addr;
	}

	socklen_t IPv4Address::getAddrLen() const {
		return sizeof(m_Addr);
	}

	std::ostream& IPv4Address::insert(std::ostream& os) const {
		uint32_t addr = byteswapOnLittleEndian(m_Addr.sin_addr.s_addr);
		os << ((addr >> 24) & 0xff) << "."
		   << ((addr >> 16) & 0xff) << "."
		   << ((addr >> 8) & 0xff) << "."
		   << (addr & 0xff);
		os << ":" << byteswapOnLittleEndian(m_Addr.sin_port);
		return os;
	}

	IPv4Address::ipAddressPtr IPv4Address::broadcastAddress(uint32_t prefixLen) {
		if (prefixLen > 32)
			return nullptr;

		sockaddr_in baddr(m_Addr);
		baddr.sin_addr.s_addr |= byteswapOnLittleEndian(CreateMask<uint32_t>(prefixLen));
		return IPv4Address::ipv4AddressPtr(new IPv4Address(baddr));
	}

	IPv4Address::ipAddressPtr IPv4Address::networkAddress(uint32_t prefixLen) {
		if (prefixLen > 32)
			return nullptr;

		sockaddr_in baddr(m_Addr);
		baddr.sin_addr.s_addr &= byteswapOnLittleEndian(CreateMask<uint32_t>(prefixLen));
		return IPv4Address::ipv4AddressPtr(new IPv4Address(baddr));
	}

	IPv4Address::ipAddressPtr IPv4Address::subnetMask(uint32_t prefixLen) {
		sockaddr_in subnet;
		memset(&subnet, 0, sizeof(subnet));
		subnet.sin_family = AF_INET;
		subnet.sin_addr.s_addr = ~byteswapOnLittleEndian(CreateMask<uint32_t>(prefixLen));
		return IPv4Address::ipv4AddressPtr(new IPv4Address(subnet));
	}

	uint32_t IPv4Address::getPort() const {
		return byteswapOnLittleEndian(m_Addr.sin_port);
	}

	void IPv4Address::setPort(uint16_t port) {
		m_Addr.sin_port = byteswapOnLittleEndian(port);
	}

	// TODO: IPv6Address and UNIXAddress

	UnknownAddress::UnknownAddress(int family) {
		memset(&m_Addr, 0, sizeof(m_Addr));
		m_Addr.sa_family = family;
	}

	UnknownAddress::UnknownAddress(const sockaddr& addr) {
		m_Addr = addr;
	}

	const sockaddr* UnknownAddress::getAddr() const {
		return &m_Addr;
	}

	sockaddr* UnknownAddress::getAddr() {
		return (sockaddr*)&m_Addr;
	}

	socklen_t UnknownAddress::getAddrLen() const {
		return sizeof(m_Addr);
	}

	std::ostream& UnknownAddress::insert(std::ostream& os) const {
		os << "[UnknownAddress family=" << m_Addr.sa_family << "]";
		return os;
	}

}
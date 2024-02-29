#pragma once
#include <memory>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <map>

namespace WebServer {

	class IPAddress;

	class Address {
	public:
		typedef std::shared_ptr<Address> addressPtr;

		static Address::addressPtr Create(const sockaddr* addr, socklen_t addrlen);
		
		/*
		* @param[out] result    保存符合条件的Address
		* @param[in]  host      域名,服务器名等
		* @param[in]  family    协议族(AF_INET, AF_INET6, AF_UNIX)
		* @param[in]  type      socket类型(SOCK_STREAM, SOCK_DGRAM等)
		* @param[in]  protocol  协议(IPPROTP_TCP, IPPROTO_UDP等)
		*/
		static bool Lookup(std::vector<Address::addressPtr>& result, const std::string& host,
			int family = AF_INET, int type = 0, int protocol = 0);

		static Address::addressPtr LookupAny(const std::string& host, 
			int family = AF_INET, int type = 0, int protocol = 0);

		static std::shared_ptr<IPAddress> LookupAnyIpAddress(const std::string& host,
			int family = AF_INET, int type = 0, int protocol = 0);

		static bool GetInterfaceAddress(std::multimap<std::string, std::pair<Address::addressPtr, uint32_t>>& result, int family = AF_INET);
		static bool GetInterfaceAddress(std::vector<std::pair<Address::addressPtr, uint32_t>>& result,
			const std::string& iface, int family = AF_INET);

		virtual ~Address() {}
		
		int getFamily() const;
		
		virtual const sockaddr* getAddr() const = 0;  // 只读
		virtual sockaddr* getAddr() = 0;              // 读写
		virtual socklen_t getAddrLen() const = 0;
		virtual std::ostream& insert(std::ostream& os) const = 0;

		std::string toString() const;
		bool operator<(const Address& rhs) const;
		bool operator==(const Address& rhs) const;
		bool operator!=(const Address& rhs) const;
	};

	class IPAddress : public Address {
	public:
		typedef std::shared_ptr<IPAddress> ipAddressPtr;
		static ipAddressPtr Create(const char* address, uint16_t port = 0);
		virtual ipAddressPtr broadcastAddress(uint32_t prefixLen) = 0;
		virtual ipAddressPtr networkAddress(uint32_t prefixLen) = 0;
		virtual ipAddressPtr subnetMask(uint32_t prefixLen) = 0;

		virtual uint32_t getPort() const = 0;
		virtual void setPort(uint16_t port) = 0;
	};

	class IPv4Address : public IPAddress {
	public:
		typedef std::shared_ptr<IPv4Address> ipv4AddressPtr;
		static ipv4AddressPtr Create(const char* address, uint16_t port = 0);
		IPv4Address(const sockaddr_in& address);
		IPv4Address(uint32_t address = INADDR_ANY, uint16_t port = 0);

		const sockaddr* getAddr() const override;
		sockaddr* getAddr() override;
		socklen_t getAddrLen() const override;
		std::ostream& insert(std::ostream& os) const override;

		ipAddressPtr broadcastAddress(uint32_t prefixLen) override;
		ipAddressPtr networkAddress(uint32_t prefixLen) override;
		ipAddressPtr subnetMask(uint32_t prefixLen) override;
		uint32_t getPort() const override;
		void setPort(uint16_t port) override;
	private:
		sockaddr_in m_Addr;
	};

	class UnknownAddress : public Address {
	public:
		typedef std::shared_ptr<UnknownAddress> unknownAddressPtr;
		UnknownAddress(int family);
		UnknownAddress(const sockaddr& addr);

		const sockaddr* getAddr() const override;
		sockaddr* getAddr() override;
		socklen_t getAddrLen() const override;
		std::ostream& insert(std::ostream& os) const override;
	private:
		sockaddr m_Addr;
	};
}
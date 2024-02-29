#pragma once
#include "address.h"

#include <memory>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
// #include <openssl/err.h>
// #include <openssl/ssl.h>

namespace WebServer {

	class Socket : public std::enable_shared_from_this<Socket> {
	public:
		typedef std::shared_ptr<Socket> socketPtr;
		typedef std::weak_ptr<Socket> socketWeakPtr;
		
		enum Type {
			TCP = SOCK_STREAM,
			UDP = SOCK_DGRAM
		};

		enum Family {
			IPv4 = AF_INET,
			IPv6 = AF_INET6,
			UNIX = AF_UNIX
		};

		static Socket::socketPtr CreateTCP(WebServer::Address::addressPtr address);
		static Socket::socketPtr CreateUDP(WebServer::Address::addressPtr address);

		static Socket::socketPtr CreateTCPSocket();
		static Socket::socketPtr CreateUDPSocket();

		static Socket::socketPtr CreateTCPSocket6();
		static Socket::socketPtr CreateUDPSocket6();

		// static Socket::socketPtr CreateUnixTCPSocket();
		// static Socket::socketPtr CreateUnixUDPSocket();

		Socket(int family, int type, int protocol = 0);
		virtual ~Socket();

		int64_t getSendTimeout();
		void setSendTimeout(int64_t timeout);

		int64_t getReceiveTimeout();
		void setReceiveTimeout(int64_t timeout);

		bool getOption(int level, int option, void* result, socklen_t* len);

		template<typename T>
		bool getOption(int level, int option, T& result) {
			socklen_t length = sizeof(T);
			return getOption(level, option, &result, &length);
		}

		bool setOption(int level, int option, const void* result, socklen_t len);
		
		template<class T>
		bool setOption(int level, int option, const T& value) {
			return setOption(level, option, &value, sizeof(T));
		}

		/*
		* @brief   接收connect链接
		* @return  成功返回新链接的socket,失败返回nullptr
		*/
		virtual Socket::socketPtr accept();
		
		virtual bool bind(const Address::addressPtr addr);

		virtual bool connect(const Address::addressPtr addr, uint64_t timeoutMs = -1);
		virtual bool reconnect(uint64_t timeoutMs = -1);

		virtual bool listen(int backlog = SOMAXCONN);
		virtual bool close();

		virtual int send(const void* buffer, size_t length, int flags = 0);
		virtual int send(const iovec* buffers, size_t length, int flags = 0);
		virtual int sendTo(const void* buffer, size_t length, const Address::addressPtr to, int flags = 0);
		virtual int sendTo(const iovec* buffers, size_t length, const Address::addressPtr to, int flags = 0);
		virtual int receive(void* buffer, size_t length, int flags = 0);
		virtual int receive(iovec* buffers, size_t length, int flags = 0);
		virtual int receiveFrom(void* buffer, size_t length, Address::addressPtr from, int flags = 0);
		virtual int receiveFrom(iovec* buffers, size_t length, Address::addressPtr from, int flags = 0);

		Address::addressPtr getRemoteAddress();
		Address::addressPtr getLocalAddress();

		int getSocket() const { return m_Sock; }
		int getFamily() const { return m_Family; }
		int getType() const { return m_Type; }
		int getProtocol() const { return m_Protocol; }
		bool isConnected() const { return m_IsConnected; }

		bool isValid() const;
		int getError();
		virtual std::ostream& dump(std::ostream& os) const;
		virtual std::string toString() const;

		bool cancelRead();
		bool cancelWrite();
		bool cancelAccept();
		bool cancelAll();

	protected:
		void initSock();
		void newSock();
		virtual bool init(int sock);
	private:
		int m_Sock;
		int m_Family;
		int m_Type;
		int m_Protocol;
		bool m_IsConnected;

		Address::addressPtr m_LocalAddress;
		Address::addressPtr m_RemoteAddress;
	};
}
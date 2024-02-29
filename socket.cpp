#include "socket.h"
#include "core.h"
#include "hook.h"
#include "fdmanager.h"
#include "iomanager.h"
#include <limits.h>
#include <sstream>
#include <sys/socket.h>

namespace WebServer {

	Socket::socketPtr Socket::CreateTCP(WebServer::Address::addressPtr address) {
		Socket::socketPtr sock(new Socket(address->getFamily(), TCP, 0));
		return sock;
	}

	Socket::socketPtr Socket::CreateUDP(WebServer::Address::addressPtr address) {
		Socket::socketPtr sock(new Socket(address->getFamily(), UDP, 0));
		sock->newSock();
		sock->m_IsConnected = true;
		return sock;
	}

	Socket::socketPtr Socket::CreateTCPSocket()	{
		Socket::socketPtr sock(new Socket(IPv4, TCP, 0));
		return sock;
	}

	Socket::socketPtr Socket::CreateUDPSocket()
	{
		Socket::socketPtr sock(new Socket(IPv4, UDP, 0));
		// 可能是因为UDP是一种无连接的协议,因此直接在这里就把套接字的文件描述符创建了,但是TCP可能后续还要判断
		sock->newSock();
		sock->m_IsConnected = true;
		return sock;
	}

	Socket::socketPtr Socket::CreateTCPSocket6()
	{
		Socket::socketPtr sock(new Socket(IPv6, TCP, 0));
		return sock;
	}

	Socket::socketPtr Socket::CreateUDPSocket6()
	{
		Socket::socketPtr sock(new Socket(IPv6, UDP, 0));
		sock->newSock();
		sock->m_IsConnected = true;
		return sock;
	}

	Socket::Socket(int family, int type, int protocol)
		: m_Sock(-1), m_Family(family), m_Type(type), m_Protocol(protocol), m_IsConnected(false)
	{

	}

	Socket::~Socket() {
		close();
	}

	int64_t Socket::getSendTimeout() {
		FdContext::fdContextptr context = FdMgr::GetInstance()->get(m_Sock);
		if (context)
			return context->getTimeout(SO_SNDTIMEO);
		return -1;
	}

	void Socket::setSendTimeout(int64_t timeout) {
		struct timeval tv{ int(timeout / 1000), int(timeout % 1000 * 1000) };
		setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
	}

	int64_t Socket::getReceiveTimeout() {
		FdContext::fdContextptr context = FdMgr::GetInstance()->get(m_Sock);
		if (context)
			return context->getTimeout(SO_RCVTIMEO);
		return -1;
	}

	void Socket::setReceiveTimeout(int64_t timeout) {
		struct timeval tv { int(timeout / 1000), int(timeout % 1000 * 1000) };
		setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
	}

	bool Socket::getOption(int level, int option, void* result, socklen_t* len) {
		int rt = getsockopt(m_Sock, level, option, result, (socklen_t*)len);
		if (rt) {
			std::cout << "getOption sock=" << m_Sock
				<< " level=" << level << " option=" << option
				<< " errno=" << errno << " errstr=" << strerror(errno);
			return false;
		}
		return true;
	}

	bool Socket::setOption(int level, int option, const void* result, socklen_t len) {
		if (setsockopt(m_Sock, level, option, result, len)) {
			std::cout << "setOption sock=" << m_Sock
				<< " level=" << level << " option=" << option
				<< " errno=" << errno << " errstr=" << strerror(errno);
			return false;
		}
		return true;
	}

	bool Socket::init(int sock) {
		FdContext::fdContextptr context = FdMgr::GetInstance()->get(sock);
		if (context && context->isSocked() && !context->isClose()) {
			m_Sock = sock;
			m_IsConnected = true;
			initSock();
			getLocalAddress();
			getRemoteAddress();
			return true;
		}
		return false;
	}

	bool Socket::bind(const Address::addressPtr addr) {
		if (!isValid()) {
			newSock();
			if (WS_UNLIKELY(!isValid()))
				return false;
		}

		if (WS_UNLIKELY(addr->getFamily() != m_Family)) {
			std::cout << "bind sock.family("
				<< m_Family << ") addr.family(" << addr->getFamily()
				<< ") not equal, addr=" << addr->toString();
			return false;
		}

		// TODO: UnixAddress

		// ::表示全局命名空间下的函数,即不使用某个特定命名空间下的函数
		if (::bind(m_Sock, addr->getAddr(), addr->getAddrLen())) {
			std::cout << "bind error errrno=" << errno
				<< " errstr=" << strerror(errno);
				return false;
		}
		getLocalAddress();
		return true;
	}

	bool Socket::connect(const Address::addressPtr addr, uint64_t timeoutMs) {
		m_RemoteAddress = addr;
		if (!isValid()) {
			newSock();
			if (WS_UNLIKELY(!isValid()))
				return false;
		}

		if (WS_UNLIKELY(addr->getFamily() != m_Family)) {
			std::cout << "connect sock.family("
				<< m_Family << ") addr.family(" << addr->getFamily()
				<< ") not equal, addr=" << addr->toString();
			return false;
		}

		if (timeoutMs == (uint64_t)-1) {
			if (::connect(m_Sock, addr->getAddr(), addr->getAddrLen())) {
				std::cout << "sock=" << m_Sock << " connect(" << addr->toString()
					<< ") error errno=" << errno << " errstr=" << strerror(errno);
				close();
				return false;
			}
		}
		else {
			if (::connect_with_timeout(m_Sock, addr->getAddr(), addr->getAddrLen(), timeoutMs)) {
				std::cout << "sock=" << m_Sock << " connect(" << addr->toString()
					<< ") timeout=" << timeoutMs << " error errno="
					<< errno << " errstr=" << strerror(errno);
				close();
				return false;
			}
		}
		m_IsConnected = true;
		getRemoteAddress();
		getLocalAddress();
		return true;
	}

	bool Socket::reconnect(uint64_t timeoutMs) {
		if (!m_RemoteAddress) {
			std::cout << "[error] reconnect m_remoteAddress is null";
			return false;
		}
		m_LocalAddress.reset();
		return connect(m_RemoteAddress, timeoutMs);
	}

	bool Socket::listen(int backlog) {
		if (!isValid()) {
			std::cout << "listen error sock=-1";
			return false;
		}

		if (::listen(m_Sock, backlog)) {
			std::cout << "listen error errno=" << errno
				<< " errstr=" << strerror(errno);
			return false;
		}
		return true;
	}

	Socket::socketPtr Socket::accept()
	{
		Socket::socketPtr sock(new Socket(m_Family, m_Type, m_Protocol));
		int newsock = ::accept(m_Sock, nullptr, nullptr);
		if (newsock == -1) {
			std::cout << "accept(" << m_Sock << ") errno="
				<< errno << " errstr=" << strerror(errno);
			return nullptr;
		}

		if (sock->init(newsock))
			return sock;
		return nullptr;
	}

	bool Socket::close() {
		if(!m_IsConnected && m_Sock == -1)
			return true;

		m_IsConnected = false;
		if (m_Sock != -1) {
			::close(m_Sock);
			m_Sock = -1;
		}
		return false;
	}

	int Socket::send(const void* buffer, size_t length, int flags) {
		if (m_IsConnected)
			return ::send(m_Sock, buffer, length, flags);
		return -1;
	}

	/*
	msg.msg_name = nullptr     目标地址
    msg.msg_namelen = 0        目标地址长度
    msg.msg_iov = iov;         数据缓冲区描述
    msg.msg_iovlen = 1;        数据缓冲区个数
    msg.msg_control = NULL;    辅助数据
    msg.msg_controllen = 0;    辅助数据长度
    msg.msg_flags = 0;         操作标志
	*/
	int Socket::send(const iovec* buffers, size_t length, int flags) {
		if (m_IsConnected) {
			msghdr msg;
			memset(&msg, 0, sizeof(msg));
			msg.msg_iov = (iovec*)buffers;
			msg.msg_iovlen = length;
			return ::sendmsg(m_Sock, &msg, flags);
		}
		return -1;
	}

	int Socket::sendTo(const void* buffer, size_t length, const Address::addressPtr to, int flags) {
		if (m_IsConnected)
			return ::sendto(m_Sock, buffer, length, flags, to->getAddr(), to->getAddrLen());
		return -1;
	}

	int Socket::sendTo(const iovec* buffers, size_t length, const Address::addressPtr to, int flags) {
		if (m_IsConnected) {
			msghdr msg;
			memset(&msg, 0, sizeof(msg));
			msg.msg_iov = (iovec*)buffers;
			msg.msg_iovlen = length;
			msg.msg_name = to->getAddr();
			msg.msg_namelen = to->getAddrLen();
			return ::sendmsg(m_Sock, &msg, flags);
		}
		return -1;
	}

	int Socket::receive(void* buffer, size_t length, int flags) {
		if (m_IsConnected)
			return ::recv(m_Sock, buffer, length, flags);
		return -1;
	}

	int Socket::receive(iovec* buffers, size_t length, int flags) {
		if (m_IsConnected) {
			msghdr msg;
			memset(&msg, 0, sizeof(msg));
			msg.msg_iov = (iovec*)buffers;
			msg.msg_iovlen = length;
			return ::recvmsg(m_Sock, &msg, flags);
		}
		return -1;
	}

	int Socket::receiveFrom(void* buffer, size_t length, Address::addressPtr from, int flags) {
		if (m_IsConnected) {
			socklen_t len = from->getAddrLen();
			return ::recvfrom(m_Sock, buffer, length, flags, from->getAddr(), &len);
		}
		return -1;
	}

	int Socket::receiveFrom(iovec* buffers, size_t length, Address::addressPtr from, int flags) {
		if (m_IsConnected) {
			msghdr msg;
			memset(&msg, 0, sizeof(msg));
			msg.msg_iov = (iovec*)buffers;
			msg.msg_iovlen = length;
			msg.msg_name = from->getAddr();
			msg.msg_namelen = from->getAddrLen();
			return ::recvmsg(m_Sock, &msg, flags);
		}
		return -1;
	}

	Address::addressPtr Socket::getLocalAddress() {
		if (m_LocalAddress)
			return m_LocalAddress;

		Address::addressPtr result;
		switch (m_Family) {
		case AF_INET:
			result.reset(new IPv4Address());
			break;
		
		// TODO: IPv6Address / UNIXAddress
		
		default:
			result.reset(new UnknownAddress(m_Family));
			break;
		}

		socklen_t addrLen = result->getAddrLen();
		if (getsockname(m_Sock, result->getAddr(), &addrLen)) {
			std::cout << "getsockname error sock=" << m_Sock
				<< " errno=" << errno << " errstr=" << strerror(errno);
			return Address::addressPtr(new UnknownAddress(m_Family));
		}

		//if (m_Family == AF_UNIX) {
		//	UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
		//	addr->setAddrLen(addrlen);
		//}
		m_LocalAddress = result;

		return m_LocalAddress;
	}

	Address::addressPtr Socket::getRemoteAddress() {

		if (m_RemoteAddress)
			return m_RemoteAddress;

		Address::addressPtr result;
		switch (m_Family) {
		case AF_INET:
			result.reset(new IPv4Address());
			break;

			// TODO: IPv6Address / UNIXAddress

		default:
			result.reset(new UnknownAddress(m_Family));
			break;
		}

		socklen_t addrLen = result->getAddrLen();
		if (getsockname(m_Sock, result->getAddr(), &addrLen)) {
			std::cout << "getsockname error sock=" << m_Sock
				<< " errno=" << errno << " errstr=" << strerror(errno);
			return Address::addressPtr(new UnknownAddress(m_Family));
		}

		//if (m_Family == AF_UNIX) {
		//	UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
		//	addr->setAddrLen(addrlen);
		//}
		m_RemoteAddress = result;

		return m_RemoteAddress;
	}

	void Socket::initSock() {
		int val = 1;
		setOption(SOL_SOCKET, SO_REUSEADDR, val);
		if (m_Type == SOCK_STREAM)
			setOption(IPPROTO_TCP, TCP_NODELAY, val);
	}

	void Socket::newSock() {
		m_Sock = socket(m_Family, m_Type, m_Protocol);
		if (WS_LIKELY(m_Sock) != -1)
			initSock();
		else {
			std::cout << "socket(" << m_Family
				<< ", " << m_Type << ", " << m_Protocol << ") errno="
				<< errno << " errstr=" << strerror(errno);
		}
	}

	bool Socket::isValid() const {
		return m_Sock != -1;
	}

	int Socket::getError() {

		return 0;
	}

	std::ostream& Socket::dump(std::ostream& os) const {
		os << "[Socket sock=" << m_Sock
		   << " is_connected=" << m_IsConnected
		   << " family=" << m_Family
		   << " type=" << m_Type
		   << " protocol=" << m_Protocol;
		if (m_LocalAddress)
			os << "local address=" << m_LocalAddress;

		if (m_RemoteAddress)
			os << "remote address=" << m_RemoteAddress;
		os << "]";
		return os;
	}

	std::string Socket::toString() const {
		std::stringstream ss;
		dump(ss);
		return ss.str();
	}

	bool Socket::cancelRead() {
		return IOManager::getThis()->cancelEvent(m_Sock, IOManager::READ);
	}
	
	bool Socket::cancelWrite() {
		return IOManager::getThis()->cancelEvent(m_Sock, IOManager::WRITE);
	}

	bool Socket::cancelAccept() {
		return IOManager::getThis()->cancelEvent(m_Sock, IOManager::READ);
	}

	bool Socket::cancelAll() {
		return IOManager::getThis()->cancelAll(m_Sock);
	}
}
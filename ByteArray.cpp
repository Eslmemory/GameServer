#include "ByteArray.h"
#include "endian.h"
#include <sstream>
#include <string.h>
#include <iomanip>
#include <cmath>

namespace WebServer {

	static uint32_t EncodeZigzag32(const int32_t& v) {
		if (v < 0)
			return ((uint32_t)(-v)) * 2 - 1;
		else
			return v * 2;
	}

	static uint64_t EncodeZigzag64(const int64_t& v) {
		if (v < 0)
			return ((uint32_t)(-v)) * 2 - 1;
		else
			return v * 2;
	}

	static int32_t DecodeZigzag32(const uint32_t& v) {
		return (v >> 1) ^ -(v & 1);
	}

	static int64_t DecodeZigzag64(const uint64_t& v) {
		return (v >> 1) ^ -(v & 1);
	}
	
	ByteArray::Node::Node(size_t s)
		:ptr(new char[s]), next(nullptr), size(s)
	{
	}
	
	ByteArray::Node::Node() 
		:ptr(nullptr), next(nullptr), size(0)
	{

	}

	ByteArray::Node::~Node() {
		if (ptr)
			delete[] ptr;
	}

	ByteArray::ByteArray(size_t baseSize)
		:m_BaseSize(baseSize)
		,m_Position(0)
		,m_Capacity(baseSize)
		,m_Size(0)
		,m_Endian(WS_BIG_ENDIAN)
		,m_Root(new Node(baseSize))
		,m_Cur(m_Root)
	{
	}

	ByteArray::~ByteArray() {
		Node* tmp = m_Root;
		while (tmp) {
			m_Cur = tmp;
			tmp = tmp->next;
			delete[] m_Cur;
		}
	}

	void ByteArray::writeFint8(int8_t value) {
		write(&value, sizeof(value));
	}

	void ByteArray::writeFuint8(uint8_t value) {
		write(&value, sizeof(value));
	}

	void ByteArray::writeFint16(int16_t value) {
		// 计算机中每个地址单元存储一个字节,因此int8和uint8不用转
		if (m_Endian != WS_BYTE_ORDER)
			value = byteswap(value);
		write(&value, sizeof(value));
	}

	void ByteArray::writeFuint16(uint16_t value) {
		if (m_Endian != WS_BYTE_ORDER)
			value = byteswap(value);
		write(&value, sizeof(value));
	}

	void ByteArray::writeFint32(int32_t value) {
		if(m_Endian != WS_BYTE_ORDER)
			value = byteswap(value);
		write(&value, sizeof(value));
	}

	void ByteArray::writeFuint32(uint32_t value) {
		if (m_Endian != WS_BYTE_ORDER)
			value = byteswap(value);
		write(&value, sizeof(value));
	}

	void ByteArray::writeFint64(int64_t value) {
		if (m_Endian != WS_BYTE_ORDER)
			value = byteswap(value);
		write(&value, sizeof(value));
	}

	void ByteArray::writeFuint64(uint64_t value) {
		if (m_Endian != WS_BYTE_ORDER)
			value = byteswap(value);
		write(&value, sizeof(value));
	}

	// 有符号的变成无符号的,方便进行统一压缩
	void ByteArray::writeInt32(int32_t value) {
		writeUint32(EncodeZigzag32(value));
	}

	void ByteArray::writeUint32(uint32_t value) {
		uint8_t tmp[5];
		uint8_t i = 0;
		while (value >= 0x80) {
			tmp[i++] = (value & 0x7F) | 0x80;
			value >>= 7;
		}
		tmp[i++] = value;
		write(tmp, i);
	}

	void ByteArray::writeInt64(int64_t value) {
		writeUint64(EncodeZigzag64(value));
	}

	void ByteArray::writeUint64(uint64_t value) {
		uint8_t tmp[10];
		uint8_t i = 0;

		while (value >= 0x80) {
			tmp[i++] = (value & 0x7F) | 0x80;
			value >>= 7;
		}
		tmp[i++] = value;
		write(tmp, i);
	}

	// float是4字节,uint32_t也是4字节
	void ByteArray::writeFloat(float value) {
		uint32_t v;
		memcpy(&v, &value, sizeof(value));
		writeFuint32(v);
	}

	// double是8字节
	void ByteArray::writeDouble(double value) {
		uint64_t v;
		memcpy(&v, &value, sizeof(value));
		writeFuint64(v);
	}

	void ByteArray::writeStringF16(const std::string& value) {
		writeFuint16(value.size());
		write(value.c_str(), value.size());
	}

	void ByteArray::writeStringF32(const std::string& value) {
		writeFuint32(value.size());
		write(value.c_str(), value.size());
	}

	void ByteArray::writeStringF64(const std::string& value) {
		writeFuint64(value.size());
		write(value.c_str(), value.size());
	}

	void ByteArray::writeStringVint(const std::string& value) {
		writeUint64(value.size());
		write(value.c_str(), value.size());
	}

	void ByteArray::writeStringWithoutLength(const std::string& value) {
		write(value.c_str(), value.size());
	}

	int8_t ByteArray::readFint8() {
		int8_t v;
		read(&v, sizeof(v));
		return v;
	}

	uint8_t ByteArray::readFuint8() {
		uint8_t v;
		read(&v, sizeof(v));
		return v;
	}

#define FUNC(type) \
	type v; \
	read(&v, sizeof(v)); \
	if (m_Endian == WS_BYTE_ORDER) \
		return v; \
	else \
		return byteswap(v);

	int16_t ByteArray::readFint16() {
		FUNC(int16_t);
	}

	uint16_t ByteArray::readFuint16() {
		FUNC(uint16_t);
	}

	int32_t ByteArray::readFint32() {
		FUNC(int32_t);
	}

	uint32_t ByteArray::readFuint32() {
		FUNC(uint32_t);
	}

	int64_t ByteArray::readFint64() {
		FUNC(int64_t);
	}

	uint64_t ByteArray::readFuint64() {
		FUNC(uint64_t);
	}
#undef FUNC

	int32_t ByteArray::readInt32() {
		return DecodeZigzag32(readUint32());
	}

	uint32_t ByteArray::readUint32() {
		uint32_t result = 0;
		for(int i = 0; i < 32; i+=7){
			uint8_t b = readFuint8();
			if(b < 0x80){
				result |= ((uint32_t)b) << i;
				break;
			}
			else {
				result |= (((uint32_t)(b & 0x7F)) << i);
			}
		}
		return result;
	}

	int64_t ByteArray::readInt64() {
		return DecodeZigzag64(readUint64());
	}

	uint64_t ByteArray::readUint64() {
		uint64_t result = 0;
		for (int i = 0; i < 64; i++) {
			uint8_t b = readFuint8();
			if (b < 0x80) {
				result |= ((uint64_t)b) << i;
				return result;
			}
			else {
				result |= ((uint64_t)((b & 0x7F) << i));
			}
		}
		return result;
	}

	float ByteArray::readFloat() {
		uint32_t v = readUint32();
		float value;
		memcpy(&value, &v, sizeof(v));
		return v;
	}

	double ByteArray::readDouble() {
		uint64_t v = readUint64();
		double value;
		memcpy(&value, &v, sizeof(v));
		return v;
	}

	std::string ByteArray::readStringF16() {
		uint16_t len = readFuint16();
		std::string buff;
		buff.resize(len);
		read(&buff[0], len);
		return buff;
	}

	std::string ByteArray::readStringF32() {
		uint32_t len = readFuint32();
		std::string buff;
		buff.resize(len);
		read(&buff[0], len);
		return buff;
	}

	std::string ByteArray::readStringF64() {
		uint64_t len = readFuint64();
		std::string buff;
		buff.resize(len);
		read(&buff[0], len);
		return buff;
	}

	std::string ByteArray::readStringVint() {
		uint64_t len = readUint64();
		std::string buff;
		buff.resize(len);
		read(&buff[0], len);
		return buff;
	}

	void ByteArray::setPosition(size_t v) {
		if (v > m_Capacity)
			throw std::out_of_range("set_position out if range");
		m_Position = v;
		// 相当于是扩容一下,现在m_Position比实际的数据大小m_Size大,因此把m_Size扩大
		// 由于扩大后m_Size和m_Position一样大,因此也没有读取的数据
		// 之前判断了v<=m_Capacity,因此m_Position能够到达的有效位置一定是分配了内存的 
		if (m_Position > m_Size)
			m_Size = m_Position;
		m_Cur = m_Root;
		while (v > m_Cur->size) {
			v -= m_Cur->size;
			m_Cur = m_Cur->next;
		}
		if (v == m_Cur->size)
			m_Cur = m_Cur->next;
	}

	bool ByteArray::isLittleEndian() const {
		return m_Endian == WS_LITTLE_ENDIAN;
	}

	void ByteArray::setIsLittleEndian(bool val) {
		if (val)
			m_Endian = WS_LITTLE_ENDIAN;
		else
			m_Endian = WS_BIG_ENDIAN;
	}

	std::string ByteArray::toString() const {
		std::string str;
		str.resize(getReadSize());
		if (str.empty())
			return str;

		read(&str[0], str.size(), m_Position);
		return str;
	}

	std::string ByteArray::toHexString() const {
		std::string str = toString();
		std::stringstream ss;

		for (size_t i = 0; i < str.size(); ++i) {
			if (i > 0 && i % 32 == 0) {
				ss << std::endl;
			}
			ss << std::setw(2) << std::setfill('0') << std::hex
				<< (int)(uint8_t)str[i] << " ";
		}

		return ss.str();
	}

	void ByteArray::addCapacity(size_t size) {
		if (size == 0)
			return;
		size_t oldCap = getCapacity();
		// size_t oldCap = m_Cur->size - m_Position % m_BaseSize;
		if (oldCap >= size)
			return;
		size = size - oldCap;
		size_t count = ceil(1.0f * size / m_BaseSize);
		Node* tmp = m_Root;
		while (tmp->next)
			tmp = tmp->next;

		Node* first = nullptr;
		for (size_t i = 0; i < count; i++) {
			tmp->next = new Node(m_BaseSize);
			if (first == nullptr)
				first = tmp->next;
			tmp = tmp->next;
			m_Capacity += m_BaseSize;
		}

		// 最开始baseSize分配的是0,就会存在这种情况
		if (getCapacity() == 0)
			m_Cur = first;
	}

	void ByteArray::clear() {
		m_Position = m_Size = 0;
		m_Capacity = m_BaseSize;
		Node* tmp = m_Root->next;
		while (tmp) {
			m_Cur = tmp;
			tmp = tmp->next;
			delete[] m_Cur;
		}
		m_Cur = m_Root;
		m_Root->next = nullptr;
	}

	void ByteArray::write(const void* buf, size_t size) {
		if (size == 0)
			return;
		addCapacity(size);

		// 确定当前内存块剩余的内存能否继续写入数据
		size_t npos = m_Position % m_BaseSize;
		size_t ncap = m_Cur->size - npos;
		size_t bpos = 0;

		while (size > 0) {
			if (ncap >= size) {
				memcpy(m_Cur->ptr + npos, (const char*)buf + bpos, size);
				if (m_Cur->size == (npos + size))
					m_Cur = m_Cur->next;
				m_Position += size;
				bpos += size;
				size = 0;
			}
			else {
				memcpy(m_Cur->ptr, (const char*)buf + bpos, ncap);
				m_Position += ncap;
				bpos += ncap;
				size -= ncap;
				m_Cur = m_Cur->next;
				ncap = m_Cur->size;
				npos = 0;
			}
		}

		// 写入完后m_Size还没有更新
		if (m_Position > m_Size)
			m_Size = m_Position;
	}

	void ByteArray::read(void* buf, size_t size) {
		if (size > getReadSize())
			std::out_of_range("not enough len");

		size_t npos = m_Position % m_BaseSize;
		size_t ncap = m_Cur->size - npos;
		size_t bpos = 0;

		while (size > 0) {
			if (ncap >= size) {
				memcpy((char*)buf + bpos, m_Cur->ptr + npos, size);
				if (m_Cur->size == (npos + size))
					m_Cur = m_Cur->next;
				m_Position += size;
				bpos += size;
				size = 0;
			}
			else {
				memcpy((char*)buf + bpos, m_Cur->ptr + npos, ncap);
				m_Position += ncap;
				bpos += ncap;
				size -= ncap;
				m_Cur = m_Cur->next;
				ncap = m_Cur->size;
				npos = 0;
			}
		}
	}

	void ByteArray::read(void* buf, size_t size, size_t position) const {
		if (size > (m_Size - position))
			throw std::out_of_range("out enough len");

		size_t npos = position % m_BaseSize;
		size_t ncap = m_Cur->size - npos;
		size_t bpos = 0;
		Node* cur = m_Cur;
		while (size > 0) {
			if (ncap >= size) {
				memcpy((char*)buf + bpos, m_Cur->ptr + npos, size);
				if (cur->size == (npos + size))
					cur = cur->next;
				position += size;
				bpos += size;
				size = 0;
			}
			else {
				memcpy((char*)buf + bpos, m_Cur->ptr + npos, ncap);
				position += ncap;
				bpos += ncap;
				size -= ncap;
				cur = cur->next;
				ncap = cur->size;
				npos = 0;
			}
				
		}
	}

	// TODO: writeToFile
	bool ByteArray::writeToFile(const std::string& name) const {
		return false;
	}

	// TODO: readFromFile
	bool ByteArray::readFromFile(const std::string& name) {
		return false;
	}

	uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers, uint64_t len) const {
		len = len > getReadSize() ? getReadSize() : len;
		if (len == 0)
			return 0;

		uint64_t readSize = len;

		size_t npos = m_Position % m_BaseSize;
		size_t ncap = m_Cur->size - npos;
		struct iovec iov;
		Node* cur = m_Cur;

		while (len > 0) {
			if (ncap >= len) {
				iov.iov_base = cur->ptr + npos;
				iov.iov_len = len;
				len = 0;
			}
			else {
				iov.iov_base = cur->ptr + npos;
				iov.iov_len = ncap;
				len -= ncap;
				cur = cur->next;
				ncap = cur->size;
				npos = 0;
			}
			buffers.push_back(iov);
		}
		return readSize;
	}

	uint64_t ByteArray::getReadBuffers(std::vector<iovec>& buffers, uint64_t len, uint64_t position) const {
		len = len > getReadSize() ? getReadSize() : len;
		if (len == 0)
			return 0;

		uint64_t readSize = len;

		size_t npos = position % m_BaseSize;
		size_t count = position / m_BaseSize;
		Node* cur = m_Root;
		while (count != 0) {
			cur = cur->next;
			--count;
		}

		size_t ncap = cur->size - npos;
		struct iovec iov;
		while (len) {
			if (ncap >= len) {
				iov.iov_base = cur->ptr + npos;
				iov.iov_len = len;
				npos += len;
				ncap -= len;
				len = 0;
			}
			else {
				iov.iov_base = cur->ptr + npos;
				iov.iov_len = ncap;
				cur = cur->next;
				ncap = cur->size;
				npos = 0;
				len -= ncap;
			}
			buffers.push_back(iov);
		}

		return readSize;
	}

	uint64_t ByteArray::getWriteBuffers(std::vector<iovec>& buffers, uint64_t len) {
		if (len == 0)
			return 0;

		addCapacity(len);
		uint64_t writeSize = len;

		size_t npos = m_Position % m_BaseSize;
		size_t ncap = m_Cur->size - npos;
		struct iovec iov;
		Node* cur = m_Cur;
		while (len > 0) {
			if (ncap >= len) {
				iov.iov_base = cur->ptr + npos;
				iov.iov_len = len;
				len = 0;
			}
			else {
				iov.iov_base = cur->ptr + npos;
				iov.iov_len = ncap;
				len -= ncap;
				cur = cur->next;
				ncap = cur->size;
				npos = 0;
			}
			buffers.push_back(iov);
		}
		return writeSize;
	}

}
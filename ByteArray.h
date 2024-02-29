#pragma once
#include <memory>
#include <vector>
#include <stdint.h>
#include <sys/socket.h>

namespace WebServer {
	class ByteArray {
	public:
		typedef std::shared_ptr<ByteArray> bytearrayPtr;

		struct Node {
			/*
			* @param[in] s �ڴ���ֽ���
			*/			
			Node(size_t s);
			Node();
			~Node();

			char* ptr;
			Node* next;
			size_t size;
		};

		ByteArray(size_t baseSize = 4096);
		~ByteArray();

		// ԭʼ����ֱ�Ӵ洢
		void writeFint8(int8_t value);
		void writeFuint8(uint8_t value);
		void writeFint16(int16_t value);
		void writeFuint16(uint16_t value);
		void writeFint32(int32_t value);
		void writeFuint32(uint32_t value);
		void writeFint64(int64_t value);
		void writeFuint64(uint64_t value);

		// ����ѹ����洢
		void writeInt32(int32_t value);
		void writeUint32(uint32_t value);
		void writeInt64(int64_t value);
		void writeUint64(uint64_t value);

		void writeFloat(float value);
		void writeDouble(double value);

		// �Թ̶����������ʹ洢��������
		void writeStringF16(const std::string& value);
		void writeStringF32(const std::string& value);
		void writeStringF64(const std::string& value);
		
		// �Գ�������Ҳ���б���ѹ��
		void writeStringVint(const std::string& value);
		
		void writeStringWithoutLength(const std::string& value);

		int8_t readFint8();
		uint8_t readFuint8();
		int16_t  readFint16();
		uint16_t readFuint16();
		int32_t  readFint32();
		uint32_t readFuint32();
		int64_t  readFint64();
		uint64_t readFuint64();

		int32_t  readInt32();
		uint32_t readUint32();
		int64_t  readInt64();
		uint64_t readUint64();

		float    readFloat();
		double   readDouble();

		std::string readStringF16();
		std::string readStringF32();
		std::string readStringF64();
		std::string readStringVint();

		void clear();
		void write(const void* buf, size_t size);
		void read(void* buf, size_t size);
		void read(void* buf, size_t size, size_t position) const;
		bool writeToFile(const std::string& name) const;
		bool readFromFile(const std::string& name);

		size_t getPosition() const { return m_Position; }
		void setPosition(size_t v);
		size_t getBaseSize() const { return m_BaseSize; }
		size_t getReadSize() const { return m_Size - m_Position; }
		bool isLittleEndian() const;
		void setIsLittleEndian(bool val);
		std::string toString() const;
		std::string toHexString() const;

		uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len = ~0ull) const;
		uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len, uint64_t position) const;
		uint64_t getWriteBuffers(std::vector<iovec>& buffers, uint64_t len);
		size_t getSize() const { return m_Size; }

	private:
		void addCapacity(size_t size);
		size_t getCapacity() const { return m_Capacity; }

	private:
		// �ڴ���С
		size_t m_BaseSize;
		// ��ǰ����λ��,ָ�����ݵ���һ��λ��
		size_t m_Position;
		// ��ǰ��������
		size_t m_Capacity;
		// ��ǰ���ݵĴ�С
		size_t m_Size;
		// �ֽ���,Ĭ�ϴ��
		int8_t m_Endian;
		// ��һ���ڴ���ָ��
		Node* m_Root;
		// ��ǰ�������ڴ��ָ��
		Node* m_Cur;
	};
}
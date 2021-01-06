// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_BUFFER_H
#define MUDUO_NET_BUFFER_H

#include <muduo/base/copyable.h>
#include <muduo/base/StringPiece.h>
#include <muduo/base/Types.h>

#include <muduo/net/Endian.h>

#include <algorithm>
#include <vector>

#include <assert.h>
#include <string.h>
//#include <unistd.h>  // ssize_t

namespace muduo
{
namespace net
{

/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// @endcode
class Buffer : public muduo::copyable
{
 public:
  //初始化用，假如存储helloworld。空字符用x代替。那么初始化后的内存布局是xxxx,xxxx,hell,worl,dxxxx...xxx
  //readerIndex_ 指向了index为8的位置，也就是h的位置。前面8个字符为空。
  //writerIndex_ 同样指向了h的位置。
  //初始化后，一共有1024+8=1032个字符。坐标最多追溯到0-1031。
  static const size_t kCheapPrepend = 8;
  static const size_t kInitialSize = 1024;

  Buffer()
    : buffer_(kCheapPrepend + kInitialSize),
      readerIndex_(kCheapPrepend),
      writerIndex_(kCheapPrepend)
  {
    assert(readableBytes() == 0);
    assert(writableBytes() == kInitialSize);
    assert(prependableBytes() == kCheapPrepend);
  }

  // default copy-ctor, dtor and assignment are fine

  void swap(Buffer& rhs)
  {
    buffer_.swap(rhs.buffer_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, rhs.writerIndex_);
  }

  size_t readableBytes() const
  { return writerIndex_ - readerIndex_; }

  //1032-8=1024 如果初始化后调用此函数，那么返回值为可以写入的字符数量。是1024个字节。index从8到1031截止。
  size_t writableBytes() const
  { return buffer_.size() - writerIndex_; }

  //返回readerIndex
  size_t prependableBytes() const
  { return readerIndex_; }

  const char* peek() const
  { return begin() + readerIndex_; } //begin返回首元素地址。 peek则是首地址+偏移量返回readerIndex指向的元素的地址。

  //kCRLF实际上是"\r\n" 
  const char* findCRLF() const
  {
    const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF+2);
    return crlf == beginWrite() ? NULL : crlf; //beginWrite返回的是writerIndex指向的元素的地址
  }

  //在可以读取的范围内进行查找
  const char* findCRLF(const char* start) const
  {
    assert(peek() <= start);
    assert(start <= beginWrite());
    const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF+2);
    return crlf == beginWrite() ? NULL : crlf;
  }

  // retrieve returns void, to prevent
  // string str(retrieve(readableBytes()), readableBytes());
  // the evaluation of two functions are unspecified
  //提取数据后通常要调用，主要负责readerIndex的变化
  void retrieve(size_t len)
  {
    assert(len <= readableBytes());
    if (len < readableBytes())
    {
      readerIndex_ += len; //正常提取数据操作 readerIndex前移
    }
    else
    {
      retrieveAll(); //全部提取
    }
  }

  void retrieveUntil(const char* end)
  {
    assert(peek() <= end);
    assert(end <= beginWrite());
    retrieve(end - peek());
  }

  void retrieveInt32()
  {
    retrieve(sizeof(int32_t));
  }

  void retrieveInt16()
  {
    retrieve(sizeof(int16_t));
  }

  void retrieveInt8()
  {
    retrieve(sizeof(int8_t));
  }

  void retrieveAll()
  {
    readerIndex_ = kCheapPrepend; //清空后一切回归初始化的样子
    writerIndex_ = kCheapPrepend;
  }

  string retrieveAllAsString()
  {
    return retrieveAsString(readableBytes());;
  }
  //提取数据作为string返回
  string retrieveAsString(size_t len)
  {
    assert(len <= readableBytes());
    string result(peek(), len);
    retrieve(len);
    return result;
  }


  //将可读的数据转为StringPiece 只读，不变化readerIndex
  StringPiece toStringPiece() const
  {
    return StringPiece(peek(), static_cast<int>(readableBytes()));
  }

  void append(const StringPiece& str)
  {
    append(str.data(), str.size());
  }

  //正式往Buffer中加入数据。比如写入china，5。
  void append(const char* /*restrict*/ data, size_t len)
  {
    ensureWritableBytes(len);//确保Buffer有足够的空间接纳数据
    std::copy(data, data+len, beginWrite());//将data内容写入Buffer
    hasWritten(len);//更新writerIndex
  }

  void append(const void* /*restrict*/ data, size_t len)
  {
    append(static_cast<const char*>(data), len);
  }

  void ensureWritableBytes(size_t len)
  {
    if (writableBytes() < len) //假如可写字节已经小于了用户要写入的字节数量。
    {
      makeSpace(len);
    }
    assert(writableBytes() >= len);
  }

  char* beginWrite()
  { return begin() + writerIndex_; }

  const char* beginWrite() const
  { return begin() + writerIndex_; }

  void hasWritten(size_t len)
  { writerIndex_ += len; }

  ///
  /// Append int32_t using network endian
  ///
  void appendInt32(int32_t x)
  {
    //int32_t转为网络字节序后写入Buffer
    int32_t be32 = sockets::hostToNetwork32(x);
    append(&be32, sizeof be32);
  }

  void appendInt16(int16_t x)
  {
    int16_t be16 = sockets::hostToNetwork16(x);
    append(&be16, sizeof be16);
  }

  void appendInt8(int8_t x)
  {
    append(&x, sizeof x);
  }

  ///
  /// Read int32_t from network endian
  ///
  /// Require: buf->readableBytes() >= sizeof(int32_t)
  int32_t readInt32()
  {
    int32_t result = peekInt32();
    retrieveInt32();
    return result;
  }

  int16_t readInt16()
  {
    int16_t result = peekInt16();
    retrieveInt16();
    return result;
  }

  int8_t readInt8()
  {
    int8_t result = peekInt8();
    retrieveInt8();
    return result;
  }

  ///
  /// Peek int32_t from network endian
  ///
  /// Require: buf->readableBytes() >= sizeof(int32_t)
  //由readInt32调用
  int32_t peekInt32() const
  {
    assert(readableBytes() >= sizeof(int32_t));
    int32_t be32 = 0;
    ::memcpy(&be32, peek(), sizeof be32);
    return sockets::networkToHost32(be32);
  }

  int16_t peekInt16() const
  {
    assert(readableBytes() >= sizeof(int16_t));
    int16_t be16 = 0;
    ::memcpy(&be16, peek(), sizeof be16);
    return sockets::networkToHost16(be16);
  }

  int8_t peekInt8() const
  {
    assert(readableBytes() >= sizeof(int8_t));
    int8_t x = *peek();
    return x;
  }

  ///
  /// Prepend int32_t using network endian
  ///
  void prependInt32(int32_t x)
  {
    int32_t be32 = sockets::hostToNetwork32(x);
    prepend(&be32, sizeof be32);
  }

  void prependInt16(int16_t x)
  {
    int16_t be16 = sockets::hostToNetwork16(x);
    prepend(&be16, sizeof be16);
  }

  void prependInt8(int8_t x)
  {
    prepend(&x, sizeof x);
  }

  //加入数据前，将readerIndex减少len。然后写入len长度。
  //换句话说，在readerIndex的前面加入了一部分数据
  void prepend(const void* /*restrict*/ data, size_t len)
  {
    assert(len <= prependableBytes());
    readerIndex_ -= len;
    const char* d = static_cast<const char*>(data);
    std::copy(d, d+len, begin()+readerIndex_);
  }

  void shrink(size_t reserve)
  {
    // FIXME: use vector::shrink_to_fit() in C++ 11 if possible.
    Buffer other;
    other.ensureWritableBytes(readableBytes()+reserve);//可读数据+传入数据
    other.append(toStringPiece());
    swap(other);//重新交换区间 使得capacity不再是那么大
  }

  /// Read data directly into buffer.
  ///
  /// It may implement with readv(2)
  /// @return result of read(2), @c errno is saved
  ssize_t readFd(int fd, int* savedErrno);

 private:

  char* begin()
  { return &*buffer_.begin(); }

  const char* begin() const
  { return &*buffer_.begin(); }


  //扩展空间
  void makeSpace(size_t len)
  {
    //prependableBytes()返回的是read指针指向的index。
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)
    {
      // FIXME: move readable data
      //如果可写的字符+read指针前面的字符小于要写入字符的长度加默认头的长度，代表真不够空间了，需要扩展空间。
      //比如len=10000，writerIndex假如指向500，那么直接Buffer的空间被扩展到10500。
      buffer_.resize(writerIndex_+len);
    }
    else
    {
      //逻辑走到这里证明，内部还是有腾挪的空间的。字符集体往前移动,就暂时不扩展了。
      // move readable data to the front, make space inside buffer
      assert(kCheapPrepend < readerIndex_);
      size_t readable = readableBytes();
      std::copy(begin()+readerIndex_, //src begin
                begin()+writerIndex_, //src end
                begin()+kCheapPrepend);//dst
      readerIndex_ = kCheapPrepend;
      writerIndex_ = readerIndex_ + readable;
      assert(readable == readableBytes());
    }
  }

 private:
  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;

  static const char kCRLF[];
};

}
}

#endif  // MUDUO_NET_BUFFER_H

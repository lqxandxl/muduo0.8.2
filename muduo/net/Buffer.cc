// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include <muduo/net/Buffer.h>

#include <muduo/net/SocketsOps.h>

#include <errno.h>
#include <sys/uio.h>

using namespace muduo;
using namespace muduo::net;

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

ssize_t Buffer::readFd(int fd, int* savedErrno)
{
  // saved an ioctl()/FIONREAD call to tell how much to read
  char extrabuf[65536];
  struct iovec vec[2];
  const size_t writable = writableBytes(); //可写区间的字节数
  vec[0].iov_base = begin()+writerIndex_;//可写writerIndex的数字
  vec[0].iov_len = writable;//可写区间 可以往buffer里加的长度
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
  //这里的读实际上是一共两个iovec结构体传入，数据占满第一个结构体后，去占第二个结构体。
  const ssize_t n = sockets::readv(fd, vec, 2); //系统调用，返回已读字节数 
  if (n < 0)
  {
    *savedErrno = errno;
  }
  else if (implicit_cast<size_t>(n) <= writable) //向上转型 即派生类转为父类需要implicit_cast,如果有误，编译期报错
  {
    writerIndex_ += n;
  }
  else
  {
    //读出来的字节数大于Buffer的缓存，更新writerIndex为最新。
    writerIndex_ = buffer_.size();
    append(extrabuf, n - writable); //n-可写字节数 等于超出的字节数，使用append添加到成员后面。
    //将第二个结构体中读出的数据放入Buffer中
  }
  // if (n == writable + sizeof extrabuf)
  // {
  //   goto line_30;
  // }
  return n;
}


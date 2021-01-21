#ifndef MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H
#define MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H

#include <muduo/base/Logging.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Endian.h>
#include <muduo/net/TcpConnection.h>

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

class LengthHeaderCodec : boost::noncopyable
{
 public:
  typedef boost::function<void (const muduo::net::TcpConnectionPtr&,
                                const muduo::string& message,
                                muduo::Timestamp)> StringMessageCallback;

  explicit LengthHeaderCodec(const StringMessageCallback& cb)
    : messageCallback_(cb)
  {
  }

  //TcpConnection收到数据后，回调至用户设置的此函数先进性转化
  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp receiveTime)
  {//消息的解析过程 定长的头部，以及根据头部而去解析消息体，对于tcp可靠传输来说，每一个字节都必可达，且只收到一次，这是可行的
    while (buf->readableBytes() >= kHeaderLen) // kHeaderLen == 4
    {
      // FIXME: use Buffer::peekInt32()
      const void* data = buf->peek();//可读数据的起始位置
      int32_t be32 = *static_cast<const int32_t*>(data); // SIGBUS //对该地址按四字节取数据
      const int32_t len = muduo::net::sockets::networkToHost32(be32); //进行网络字节序到主机字节序的转化
      if (len > 65536 || len < 0) //消息长度不合格的不进行处理 关闭连接
      {
        LOG_ERROR << "Invalid length " << len;
        conn->shutdown();  // FIXME: disable reading
        break;
      }
      else if (buf->readableBytes() >= len + kHeaderLen)  //可读总数据满足消息头+消息头所描述的消息体长度，则满足了可以读取的条件
      {
        buf->retrieve(kHeaderLen); //先将消息头取出
        muduo::string message(buf->peek(), len); //再提取消息体
        messageCallback_(conn, message, receiveTime); //最终回调用户的方法，这个时候直接就是完整的string了
        buf->retrieve(len); //然后把消息体也从buf中删除
      }
      else
      {
        break;
      }
    }
  }

  // FIXME: TcpConnectionPtr
  void send(muduo::net::TcpConnection* conn,
            const muduo::StringPiece& message)
  {
    muduo::net::Buffer buf; //这个buf是临时构造的，之前没有数据
    buf.append(message.data(), message.size());
    int32_t len = static_cast<int32_t>(message.size());
    int32_t be32 = muduo::net::sockets::hostToNetwork32(len);
    buf.prepend(&be32, sizeof be32); //在readIndex前追加数据
    conn->send(&buf);//最终发送数据
  }

 private:
  StringMessageCallback messageCallback_;
  const static size_t kHeaderLen = sizeof(int32_t);
};

#endif  // MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H

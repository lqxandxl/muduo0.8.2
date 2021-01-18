// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoop.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Singleton.h>
#include <muduo/net/Channel.h>
#include <muduo/net/Poller.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TimerQueue.h>

#include <boost/bind.hpp>

#include <signal.h>
#include <sys/eventfd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
__thread EventLoop* t_loopInThisThread = 0;

const int kPollTimeMs = 10000;

int createEventfd()
{
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
    LOG_TRACE << "Ignore SIGPIPE";
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
}

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

EventLoop::EventLoop()
  : looping_(false),
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()),
    poller_(Poller::newDefaultPoller(this)), //新建poller，poll还是epoll取决于环境变量
    timerQueue_(new TimerQueue(this)),//定时任务的队列
    wakeupFd_(createEventfd()),//创建一个事件的fd来监听
    wakeupChannel_(new Channel(this, wakeupFd_)), //channel管理fd，几个new都是用智能指针进行管理的
    currentActiveChannel_(NULL)
{
  LOG_TRACE << "EventLoop created " << this << " in thread " << threadId_;
  if (t_loopInThisThread) //如果为true证明该线程已有EventLoop对象。则创建失败。
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }
  wakeupChannel_->setReadCallback(
      boost::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;
}

void EventLoop::loop()
{
  assert(!looping_);
  assertInLoopThread();
  looping_ = true;
  quit_ = false;
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    activeChannels_.clear();
    //返回当前时间
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_; //最初构造是0
    if (Logger::logLevel() <= Logger::TRACE) //日志级别
    {
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true;
    for (ChannelList::iterator it = activeChannels_.begin();
        it != activeChannels_.end(); ++it)
    {
      currentActiveChannel_ = *it;
      currentActiveChannel_->handleEvent(pollReturnTime_); //调出监听的活跃fd，进行fd的处理，传入的是当前时间戳
      //EventLoop一上来使得weakupChannel处于可读状态，一旦weakupFd处于可读就会进入此逻辑。
      //handleEvent根据触发的事件类型，写，读，异常从而触发不同的回调。
      //这里weaupFd被置为不可读只是在恢复现场。假如Poller阻塞10s，那么当事件fd在第2s变成可读，就会立刻返回至此。其目的在于执行后续的doPendingFunctors方法。
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    doPendingFunctors();
  }

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

void EventLoop::quit()
{
  quit_ = true;
  if (!isInLoopThread())
  {
    wakeup();
  }
}

void EventLoop::runInLoop(const Functor& cb)
{
  if (isInLoopThread())
  {
    cb(); //如果是当前线程，那么直接执行函数
  }
  else
  {
    queueInLoop(cb); //否则进入queueInLoop函数
  }
}

void EventLoop::queueInLoop(const Functor& cb)
{
  {
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(cb);
  }

  //callingPendingFunctors_默认为False，只有正在执行函数队列的时候才是true
  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup(); //只要调用者线程和EventLoop线程不一致，就进入该函数。
    //假如一个线程内，也直接调用了queueInLoop方法，比如回调了某些方法，这些方法又回调了queueInLoop
    //如果正好处于EventLoop自己处理函数队列的过程中调用，那么就wakeup。如果不是在这个过程中，证明马上就要执行函数队列了，则不需要wakeup。
  }
}

TimerId EventLoop::runAt(const Timestamp& time, const TimerCallback& cb)
{
  return timerQueue_->addTimer(cb, time, 0.0);
}

TimerId EventLoop::runAfter(double delay, const TimerCallback& cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, cb);
}

TimerId EventLoop::runEvery(double interval, const TimerCallback& cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(cb, time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

void EventLoop::updateChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}

void EventLoop::wakeup()
{
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

void EventLoop::handleRead()
{
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one); //只读一个字节 将事件fd恢复为不可读，即weakup的fd再次被置为不可读
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  //typedef boost::function<void()> Functor; 一视同仁，都是看作一种函数来对待去调用。
  callingPendingFunctors_ = true;

  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);//互斥区短暂交换
  }

  for (size_t i = 0; i < functors.size(); ++i)
  {
    functors[i](); //执行functors下的functor
  }
  callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
  for (ChannelList::const_iterator it = activeChannels_.begin();
      it != activeChannels_.end(); ++it)
  {
    const Channel* ch = *it;
    LOG_TRACE << "{" << ch->reventsToString() << "} ";
  }
}


#include "run_loop_impl.hpp"

#include <mbgl/actor/scheduler.hpp>

#include <QCoreApplication>

#include <cassert>
#include <functional>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace mbgl {
namespace util {

void RunLoop::Impl::onReadEvent(int fd) {
    readPoll[fd].second(fd, Event::Read);
}

void RunLoop::Impl::onWriteEvent(int fd) {
    writePoll[fd].second(fd, Event::Write);
}

RunLoop* RunLoop::Get() {
    assert(static_cast<RunLoop*>(Scheduler::GetCurrent()));
    return static_cast<RunLoop*>(Scheduler::GetCurrent());
}

RunLoop::RunLoop(Type type) : impl(std::make_unique<Impl>()) {
    switch (type) {
    case Type::New:
        impl->loop = std::make_unique<QEventLoop>();
        break;
    case Type::Default:
        // Use QCoreApplication::instance().
        break;
    }

    impl->type = type;

    Scheduler::SetCurrent(this);
    impl->async = std::make_unique<AsyncTask>(std::bind(&RunLoop::process, this));
}

RunLoop::~RunLoop() {
    MBGL_VERIFY_THREAD(tid);

    Scheduler::SetCurrent(nullptr);
}

LOOP_HANDLE RunLoop::getLoopHandle() {
    throw std::runtime_error("Should not be used in Qt.");

    return nullptr;
}

void RunLoop::wake() {
    impl->async->send();
}

#ifdef __EMSCRIPTEN__
static void RunQEventLoop(void* ptr)
{
    QEventLoop* runloop = reinterpret_cast<QEventLoop*>(ptr);
    runloop->processEvents(QEventLoop::AllEvents, 100);
}
#endif

void RunLoop::run() {
    assert(QCoreApplication::instance());
    MBGL_VERIFY_THREAD(tid);

    if (impl->type == Type::Default) {
        QCoreApplication::instance()->exec();
    } else {
#ifdef __EMSCRIPTEN__
        emscripten_set_main_loop_arg(RunQEventLoop, impl->loop.get(), 0, 1);
#else
        impl->loop->exec();
#endif
    }
}

void RunLoop::stop() {
    assert(QCoreApplication::instance());
    invoke([&] {
        if (impl->type == Type::Default) {
            QCoreApplication::instance()->exit();
        } else {
            impl->loop->exit();
        }
    });
}

void RunLoop::runOnce() {
    assert(QCoreApplication::instance());
    MBGL_VERIFY_THREAD(tid);

    if (impl->type == Type::Default) {
        QCoreApplication::instance()->processEvents();
    } else {
        impl->loop->processEvents();
    }
}

void RunLoop::addWatch(int fd, Event event, std::function<void(int, Event)>&& cb) {
    MBGL_VERIFY_THREAD(tid);

    if (event == Event::Read || event == Event::ReadWrite) {
        auto notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, impl.get(), &RunLoop::Impl::onReadEvent);
        impl->readPoll[fd] = WatchPair(std::move(notifier), std::move(cb));
    }

    if (event == Event::Write || event == Event::ReadWrite) {
        auto notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Write);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, impl.get(), &RunLoop::Impl::onWriteEvent);
        impl->writePoll[fd] = WatchPair(std::move(notifier), std::move(cb));
    }
}

void RunLoop::removeWatch(int fd) {
    MBGL_VERIFY_THREAD(tid);

    auto writePollIter = impl->writePoll.find(fd);
    if (writePollIter != impl->writePoll.end()) {
        impl->writePoll.erase(writePollIter);
    }

    auto readPollIter = impl->readPoll.find(fd);
    if (readPollIter != impl->readPoll.end()) {
        impl->readPoll.erase(readPollIter);
    }
}

}
}

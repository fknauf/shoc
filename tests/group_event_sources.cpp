#include <doca/event_sources.hpp>
#include <doca/epoll_handle.hpp>

#include <gtest/gtest.h>

TEST(docapp_event_sources, epoll_eventfd) {
    auto eventfd_source = doca::event_counter{};
    auto epoll = doca::epoll_handle{};

    epoll.add_event_source(eventfd_source.eventfd());

    auto fd = epoll.wait(1);
    ASSERT_EQ(fd, -1);

    eventfd_source.increase();
    fd = epoll.wait(1);
    ASSERT_EQ(fd, eventfd_source.eventfd());

    auto count = eventfd_source.pop();
    ASSERT_EQ(1, count);

    fd = epoll.wait(1);
    ASSERT_EQ(fd, -1);

    eventfd_source.increase();
    eventfd_source.increase();
    eventfd_source.increase(3);

    fd = epoll.wait(1);
    ASSERT_EQ(fd, eventfd_source.eventfd());

    count = eventfd_source.pop();
    ASSERT_EQ(5, count);

    fd = epoll.wait(1);
    ASSERT_EQ(fd, -1);
}

TEST(docapp_event_sources, epoll_timerfd) {
    using namespace std::chrono_literals;

    auto epoll = doca::epoll_handle{};

    auto timer1 = doca::duration_timer{ 50ms };
    auto timer2 = doca::duration_timer{ 100ms };
    auto timer3 = doca::duration_timer{ 150ms };

    epoll.add_event_source(timer2.timerfd());
    epoll.add_event_source(timer3.timerfd());
    epoll.add_event_source(timer1.timerfd());

    auto fd = epoll.wait(10);

    ASSERT_EQ(fd, -1);

    fd = epoll.wait();
    ASSERT_EQ(fd, timer1.timerfd());
    ASSERT_EQ(1, timer1.pop());

    fd = epoll.wait();
    ASSERT_EQ(fd, timer2.timerfd());
    ASSERT_EQ(1, timer2.pop());

    fd = epoll.wait();
    ASSERT_EQ(fd, timer3.timerfd());
    ASSERT_EQ(1, timer3.pop());
}

TEST(docapp_event_sources, combined_sources) {
    using namespace std::chrono_literals;

    auto epoll = doca::epoll_handle{};

    auto eventfd_source = doca::event_counter{};
    auto timer1 = doca::duration_timer{ 50ms };
    auto timer2 = doca::duration_timer{ 100ms };
    auto timer3 = doca::duration_timer{ 150ms };

    epoll.add_event_source(eventfd_source.eventfd());
    epoll.add_event_source(timer2.timerfd());
    epoll.add_event_source(timer3.timerfd());
    epoll.add_event_source(timer1.timerfd());

    eventfd_source.increase();

    auto fd = epoll.wait();
    ASSERT_EQ(fd, eventfd_source.eventfd());
    ASSERT_EQ(1, eventfd_source.pop());

    fd = epoll.wait();
    ASSERT_EQ(fd, timer1.timerfd());
    ASSERT_EQ(1, timer1.pop());

    eventfd_source.increase(3);

    fd = epoll.wait();
    ASSERT_EQ(fd, eventfd_source.eventfd());
    ASSERT_EQ(3, eventfd_source.pop());

    fd = epoll.wait();
    ASSERT_EQ(fd, timer2.timerfd());
    ASSERT_EQ(1, timer2.pop());

    fd = epoll.wait();
    ASSERT_EQ(fd, timer3.timerfd());
    ASSERT_EQ(1, timer3.pop());

    eventfd_source.increase(5);

    fd = epoll.wait();
    ASSERT_EQ(fd, eventfd_source.eventfd());
    ASSERT_EQ(5, eventfd_source.pop());
}

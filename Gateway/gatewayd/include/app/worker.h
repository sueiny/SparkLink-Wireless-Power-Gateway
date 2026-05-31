#pragma once

namespace gateway::app {

class Worker {
public:
    virtual ~Worker() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void join() = 0;
    virtual const char *name() const = 0;
};

} // namespace gateway::app

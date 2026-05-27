#ifndef INJURY_CLIENT_RUNTIME_HPP
#define INJURY_CLIENT_RUNTIME_HPP

#include "injury_shell.hpp"

#include <stdint.h>

namespace injury {

enum class ClientKind : uint32_t {
    Compositor = 0,
    ShellBar = 1,
    Launcher = 2,
    QuickSettings = 3,
    Application = 4,
    WaylandBridge = 5
};

enum class ClientState : uint32_t {
    Empty = 0,
    Created = 1,
    Mapped = 2,
    Closing = 3,
    Dead = 4
};

struct Client {
    uint32_t id = 0;
    ClientKind kind = ClientKind::Application;
    ClientState state = ClientState::Empty;
    Surface *surface = nullptr;
    uint32_t serial = 0;
    uint32_t age = 0;
    bool accepts_input = true;

    bool live() const {
        return state == ClientState::Created ||
               state == ClientState::Mapped ||
               state == ClientState::Closing;
    }
};

class ClientRuntime {
public:
    static constexpr int MAX_CLIENTS = 32;

    void clear() {
        count_ = 0;
        serial_ = 1;
    }

    Client *create(ClientKind kind, Surface *surface) {
        if (!surface || count_ >= MAX_CLIENTS) return nullptr;
        Client &c = clients_[count_++];
        c.id = serial_++;
        c.kind = kind;
        c.state = ClientState::Created;
        c.surface = surface;
        c.serial = serial_++;
        c.age = 0;
        c.accepts_input = true;
        return &c;
    }

    void map(Surface *surface) {
        Client *c = find(surface);
        if (!c) return;
        if (c->state == ClientState::Mapped && c->surface->visible)
            return;
        c->state = ClientState::Mapped;
        c->surface->visible = true;
        c->serial = serial_++;
    }

    void close(Surface *surface) {
        Client *c = find(surface);
        if (!c) return;
        c->state = ClientState::Closing;
        c->accepts_input = false;
        c->serial = serial_++;
    }

    void unmap(Surface *surface) {
        Client *c = find(surface);
        if (!c) return;
        if (c->state == ClientState::Created && !c->surface->visible)
            return;
        c->state = ClientState::Created;
        c->surface->visible = false;
        c->serial = serial_++;
    }

    void tick() {
        for (int i = 0; i < count_; ++i) {
            if (clients_[i].live() && clients_[i].age < 1000000u)
                ++clients_[i].age;
        }
    }

    Client *find(Surface *surface) {
        for (int i = 0; i < count_; ++i) {
            if (clients_[i].surface == surface) return &clients_[i];
        }
        return nullptr;
    }

    const Client *find(const Surface *surface) const {
        for (int i = 0; i < count_; ++i) {
            if (clients_[i].surface == surface) return &clients_[i];
        }
        return nullptr;
    }

private:
    Client clients_[MAX_CLIENTS];
    int count_ = 0;
    uint32_t serial_ = 1;
};

} // namespace injury

#endif

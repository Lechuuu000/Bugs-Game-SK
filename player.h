#ifndef SIK_ROBAKI_PLAYER_H
#define SIK_ROBAKI_PLAYER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <ctime>
#include <netdb.h>
#include <cmath>

#include "utils.h"

class ClientId {
  public:
    const struct in6_addr address;
    const in_port_t port;
    explicit ClientId(struct sockaddr_in6 &addr) : address(addr.sin6_addr), port(addr.sin6_port) {}

    bool operator<(const ClientId &other) const {
        auto *my = (uint32_t *) &address, *his = (uint32_t *) &other.address;
        for (int i = 0; i < 4; i++) {
            if (my[i] != his[i])
                return my[i] < his[i];
        }
        return port < other.port;
    }
};

enum PlayerState : uint8_t {
    PLAYING,
    WAITING,
    READY,
    OBSERVING,
    ELIMINATED,
    DISCONNECTED
};

enum Direction : uint8_t {
    STRAIGHT = 0,
    RIGHT = 1,
    LEFT = 2,
    WRONG_DIRECTION = 3
};

class Player {
    uint64_t session_id;
    std::string name;
    PlayerState state;
    Direction last_key;
    double x, y;            // player's bug coordinates
    int16_t direction;     // in degrees
    Time time;
  public:
    Player(uint64_t session_id, Direction direction, std::string&& name_)
        : session_id(session_id), last_key(direction), name(std::move(name_)) {
        update_timestamp(time);
        state = name.empty() ? OBSERVING : WAITING;
    }
    void reset(uint64_t new_session_id, Direction dir, const char* p_name) {
        session_id = new_session_id;
        last_key = dir;
        name = std::string(p_name);
        state = (state == READY) ? READY : WAITING;
    }
    void init(double new_x, double new_y, uint16_t dir) {
        x = new_x;
        y = new_y;
        direction = dir;
    }

    void update(uint16_t turning_speed) {
        if (last_key == RIGHT) {
            direction += turning_speed;
            if (direction > 360)
                direction -= 360;
        } else if (last_key == LEFT) {
            direction -= turning_speed;
            if (direction < 0)
                direction += 360;
        }
        double theta = (double)M_PI/180*direction;
        x += cos(theta);
        y += sin(theta);

    }
    bool quiet_for_2s() {
        return elapsed_time_ms(time) >= 2000;
    }
    void update_time() {
        update_timestamp(time);
    }
    PlayerState get_state() {
        return state;
    }
    void set_state(PlayerState s) {
        if (s <= DISCONNECTED)
            state = s;
    }
    uint64_t get_session_id() const {
        return session_id;
    }

    const std::string& get_name() const{
        return name;
    }
    std::pair<int, int> get_position_int() const {
        return std::make_pair((int)x, (int)y);
    }
    void set_last_key(Direction dir) {
        last_key = dir;
    }
};

using PlayerMap = std::map<ClientId, Player>;
using PlayerMapIt = PlayerMap::iterator;

#endif //SIK_ROBAKI_PLAYER_H

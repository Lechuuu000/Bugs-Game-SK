#ifndef SIK_ROBAKI_SERVER_H
#define SIK_ROBAKI_SERVER_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctime>
#include <netdb.h>
#include <memory>
#include <map>
#include <deque>
#include <algorithm>
#include <poll.h>

#include "utils.h"
#include "message.h"
#include "player.h"

using EventIt = std::vector<std::unique_ptr<Event>>::iterator;

class Game {
    const uint16_t turning_speed;
    const uint16_t width;
    const uint16_t height;
    uint32_t seed;
    std::vector<std::vector<bool>> board;
    std::vector<PlayerMapIt> players;
    std::vector<std::unique_ptr<Event>> events;

    bool currently_being_played = false;
    uint32_t game_id = 0;
    uint8_t still_playing = 0;

    uint32_t random() {
        static bool first_call = true;
        if (first_call) {
            first_call = false;
            return seed;
        }
        seed = (uint32_t) ((uint64_t) seed * 279410273 % 4294967291);
        return seed;
    }

    bool is_position_valid(int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height)
            return false;
        return board[x][y];
    }

    bool is_position_valid(std::pair<int, int> pos) {
        return is_position_valid(pos.first, pos.second);
    }


  public:
    explicit Game(const CliOptions &o) : turning_speed(o.turning_speed), width(o.width),
                                         height(o.height), seed(o.seed),
                                         board(o.width, std::vector<bool>(o.height, false)) {}

    bool in_progress() const {
        return currently_being_played;
    }

    size_t num_of_events() const {
        return events.size();
    }

    EventIt get_event_iterator(uint32_t number) {
        return events.begin() + number;
    }

    const std::vector<std::unique_ptr<Event>> &get_events() {
        return events;
    }

    uint32_t get_id() const {
        return game_id;
    }
    uint8_t players_remaining() const {
        return still_playing;
    }

    /// returns iterator to first event
    EventIt start(std::vector<PlayerMapIt> &&new_players) {
        currently_being_played = true;
        still_playing = new_players.size();
        players = std::move(new_players);
        game_id = random();

        static auto comp = [](const PlayerMapIt &i1, const PlayerMapIt &i2) {
            return i1->second.get_name() < i2->second.get_name();
        };
        std::sort(players.begin(), players.end(), comp);
        std::vector<char> names;
        names.reserve(players.size() * 21);
        for (auto &it: players) {
            Player &p = it->second;
            // initialize bug position
            p.init(random() % width + 0.5, random() % height + 0.5, random() % 360);

            const std::string &name = it->second.get_name();
            names.insert(names.end(), name.begin(), name.end());
            names.push_back('\0');
        }

        auto event = std::make_unique<NewGameEvent>(width, height, std::move(names));
        events.push_back(std::move(event));
        for (int i = 0; i < players.size(); i++) {
            Player &p = players[i]->second;
            auto[x, y] = p.get_position_int();
            if (is_position_valid(x, y))
                events.push_back(std::make_unique<PixelEvent>(i + 1, i, x, y));
            else
                events.push_back(std::make_unique<PlayerEliminatedEvent>(i + 1, i));
        }
        return events.begin();
    }

    /// returns iterator to the first event created inside this method
    EventIt process_turn() {
        EventIt result = events.end() - 1;
        int counter = players.size();
        for (int i = 0; i < players.size(); i++) {
            Player &p = players[i]->second;
            switch (p.get_state()) {
                case DISCONNECTED:
                case ELIMINATED:
                    continue;
            }
            auto[old_x, old_y] = p.get_position_int();
            p.update(turning_speed);
            auto[new_x, new_y] = p.get_position_int();

            if (old_x == new_x && old_y == new_y)
                continue;
            if (is_position_valid(new_x, new_y)) {
                events.push_back(std::make_unique<PixelEvent>(counter++, i, new_x, new_y));
            } else {
                events.push_back(std::make_unique<PlayerEliminatedEvent>(counter++, i));
                p.set_state(ELIMINATED);
                still_playing--;
                if (still_playing == 1) {
                    currently_being_played = false;
                    break;
                }
            }
        }
        return result + 1;
    }

};


class Server {
    static const uint32_t DATAGRAM_SIZE = 550;
    static const int MAX_PLAYERS = 25;

    char buffer[2 * DATAGRAM_SIZE];
    const uint16_t rounds_per_sec;
    int socket_num = 0;
    uint16_t port;
    uint8_t ready_to_play = 0;
    PlayerMap players;
    std::vector<PlayerMapIt> waiting;
    std::deque<PlayerMapIt> player_queue;
    Game game;

    void send_data_in_buffer(size_t n_bytes, const ClientId &client) {
        sockaddr_in6 addr;
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(client.port);
        addr.sin6_addr = client.address;
        auto addr_length = (socklen_t) sizeof(addr);

        if (sendto(socket_num, buffer, DATAGRAM_SIZE, 0, (struct sockaddr *) &addr, addr_length) < 0)
            syserr("sendto");

    }

    /// sends events to a concrete client starting from *event_it
    /// if no client provided, they will be sent to all clients
    void send_events(EventIt event_it, const ClientId *client = nullptr) {
        *(uint32_t *) buffer = htonl(game.get_id());

        uint32_t offset = 4;
        for (auto it = event_it; it != game.get_events().end(); it++) {
            auto &event = *it;
            offset += event->serialize(buffer + offset);
            if (offset >= DATAGRAM_SIZE) {
                // whole datagram has been filled, it's time to send it
                if (client) {
                    send_data_in_buffer(DATAGRAM_SIZE, *client);
                } else {
                    for (auto &p_it: players)
                        send_data_in_buffer(DATAGRAM_SIZE, p_it.first);
                }
                // overflow data needs to be moved to the start of buffer
                offset -= DATAGRAM_SIZE;
                std::memcpy(buffer, buffer + DATAGRAM_SIZE, offset);
            }
        }
        if (offset > 0) {
            if (client) {
                send_data_in_buffer(offset, *client);
            } else {
                for (auto &p_it: players)
                    send_data_in_buffer(offset, p_it.first);
            }
        }
    }

    void check_activity() {
        while (!player_queue.empty()) {
            Player &p = player_queue.front()->second;
            if (p.quiet_for_2s()) {
                // player p has been quiet for too long and needs to be disconnected
                p.set_state(DISCONNECTED);
                player_queue.pop_front();
            } else {
                break;
            }
        }
    }

    void update_time_info(PlayerMapIt player_it) {
        player_it->second.update_time();
        check_activity();
        auto it = std::find(player_queue.begin(), player_queue.end(), player_it);
        if (it != player_queue.end())
            player_queue.erase(it);
        player_queue.push_back(player_it);
    }

    bool is_playername_taken(const std::string &name) {
        for (const auto &p: players) {
            if (p.second.get_name() == name)
                return true;
        }
        return false;
    }

    void process_message(const ClientMessage &m, struct sockaddr_in6 &addr) {
        if (m.turn_direction >= WRONG_DIRECTION)
            return;

        const ClientId clientId(addr);
        auto i = players.find(clientId);
        if (i == players.end()) {
            std::string name(m.player_name);
            if (is_playername_taken(name)) {
                // ignore datagram
                check_activity();
                return;
            }
            // player needs to be added to the list of existing players
            auto[it, res] = players.emplace(clientId, Player(m.session_id, m.turn_direction, std::move(name)));
            player_queue.push_back(it);
            if (it->second.get_state() == WAITING)
                waiting.push_back(it);
            update_time_info(it);
            return;
        }
        update_time_info(i);
        Player &p = i->second;
        if (p.get_session_id() > m.session_id)  // faulty datagram
            return;
        if (p.get_session_id() < m.session_id) { // player is "reconnected"
            PlayerState old_state = p.get_state();
            p.reset(m.session_id, m.turn_direction, m.player_name);
            if (old_state != WAITING && old_state != READY)
                waiting.push_back(i);
        } else {
            if (std::strcmp(m.player_name, p.get_name().data()) != 0)
                return;
            p.set_last_key(m.turn_direction);
            int n = game.num_of_events() - m.next_expected_event_no;
            if (n <= 0)
                return; // no events to send
        }
        send_events(game.get_event_iterator(m.next_expected_event_no), &clientId);
    }

    uint32_t calculate_turn_duration() {
        double milli_sec_per_turn = (double) 1000 / rounds_per_sec;
        return (uint32_t) milli_sec_per_turn;
    }

    void receive_message() {
        struct sockaddr_in6 client_addr;
        socklen_t add_size = sizeof client_addr;
        auto length = recvfrom(socket_num, buffer, DATAGRAM_SIZE, 0,
                               (struct sockaddr *) &client_addr, &add_size);
        if (length < 0)
            syserr("recvfrom");
        if (length == 0)
            return;

        ClientMessage message;
        try {
            message.deserialize(buffer, length);
        } catch (DeserializationException &e) {
            // faulty datagram, it will be ignored
            return;
        }

        process_message(message, client_addr);
    }

    void process_game() {
        static const int turn_duration_ms = calculate_turn_duration();
        struct pollfd poll_fd = {.fd=socket_num, .events = POLLIN};
        while (true) {
            // one loop iteration corresponds to one game turn
            Time time;
            // TODO process turn

            int time_remaining = turn_duration_ms;
            update_timestamp(time);
            while (int ret = poll(&poll_fd, 1, time_remaining)) {
                // receive messages until next turn needs to be processed
                if (ret < 0)
                    syserr("poll");

                receive_message();
                time_remaining = std::max(turn_duration_ms - (int) elapsed_time_ms(time), 0);
            }
        }
    }

  public:
    Server(const CliOptions &o) : port(o.port), game(o), rounds_per_sec(o.rounds_per_sec) {}

    ~Server() {
        if (socket_num) {
            close(socket_num);
        }
    }

    void run() {
        socket_num = socket(AF_INET6, SOCK_DGRAM, 0) < 0;
        if (socket_num)
            syserr("socket");
        // accept both ipv4 and ipv6
        int optval = 0;
        if (setsockopt(socket_num, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &optval, sizeof optval) < 0)
            syserr("setsockopt(IPV6_V6ONLY");
        optval = 1;
        // allow server to reuse port when restarted
        if (setsockopt(socket_num, SOL_SOCKET, SO_REUSEADDR, (char *) &optval, sizeof optval) < 0)
            syserr("setsockopt(SO_REUSEADDR)");

        struct sockaddr_in6 my_addr;
        memset(&my_addr, 0, sizeof(my_addr));
        my_addr.sin6_family = AF_INET6;
        my_addr.sin6_port = htons(port);
        my_addr.sin6_addr = in6addr_any;
        if (bind(socket_num, (struct sockaddr *) &my_addr, sizeof my_addr) < 0)
            syserr("bind");

        while (true) {
            while (waiting.size() < 2 || waiting.size() != ready_to_play) {
                receive_message();
            }
            game.start(std::move(waiting));

            process_game();
        }
    }
};

#endif //SIK_ROBAKI_SERVER_H

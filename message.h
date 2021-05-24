#ifndef SIK_ROBAKI_MESSAGE_H
#define SIK_ROBAKI_MESSAGE_H

#include "player.h"

class DeserializationException : public std::exception {
};

class SerializationException : public std::exception {

};

enum EventType : uint8_t {
    NEW_GAME = 0,
    PIXEL = 1,
    PLAYER_ELIMINATED = 2,
    GAME_OVER = 3,
    WRONG_EVENT_TYPE = 4
};

class Event {
  protected:
    uint32_t length;
    uint32_t number;
  public:
    Event(uint32_t length, uint32_t number) : length(length), number(number) {}

    /// returns the size in bytes of the data and stores it at the given address
    virtual uint32_t serialize(char *address) {
        *(uint32_t *) (address) = htonl(length);
        *(uint32_t *) (address + 4) = htonl(number);
        return 8;
    }
};

class NewGameEvent : public Event {
    uint32_t width;
    uint32_t height;
    std::vector<char> player_names;
    uint32_t player_names_bytes;
  public:
    NewGameEvent(uint32_t width, uint32_t height, std::vector<char> &&player_names)
            : Event(player_names.size() + 17, 0), width(width), height(height),
              player_names(std::move(player_names)) {}

    uint32_t serialize(char *address) override {
        Event::serialize(address);
        *(uint8_t *) (address + 8) = NEW_GAME;
        *(uint32_t *) (address + 9) = htonl(width);
        *(uint32_t *) (address + 13) = htonl(height);
        std::memcpy(address + 17, player_names.data(), player_names.size());
        uint32_t size = player_names.size() + 17;
        *(uint32_t *) (address + size) = htonl(calculate_crc32(address, size));
        return size + 4;
    }
};

class PixelEvent : public Event {
    uint8_t player_number;
    uint32_t x;
    uint32_t y;
  public:
    PixelEvent(uint32_t number, uint8_t player_number, uint32_t x, uint32_t y)
            : Event(14, number), player_number(player_number), x(x), y(y) {}

    uint32_t serialize(char *address) override {
        Event::serialize(address);
        *(uint8_t *) (address + 8) = PIXEL;
        *(uint8_t *) (address + 9) = player_number;
        *(uint32_t *) (address + 10) = htonl(x);
        *(uint32_t *) (address + 14) = htonl(y);
        *(uint32_t *) (address + 18) = htonl(calculate_crc32(address, 18));
        return 22;
    }
};

class PlayerEliminatedEvent : public Event {
    uint8_t player_number;
  public:
    PlayerEliminatedEvent(uint32_t number, uint8_t player_number)
            : Event(6, number), player_number(player_number) {}

    uint32_t serialize(char *address) override {
        Event::serialize(address);
        *(uint8_t *) (address + 8) = PLAYER_ELIMINATED;
        *(uint8_t *) (address + 9) = player_number;
        *(uint32_t *) (address + 10) = htonl(calculate_crc32(address, 10));
        return 14;
    }
};

class GameOverEvent : public Event {
  public:
    using Event::Event;

    uint32_t serialize(char *address) override {
        Event::serialize(address);
        *(uint8_t *) (address + 8) = GAME_OVER;
        *(uint32_t *) (address + 9) = htonl(calculate_crc32(address, 9));
        return 13;
    }
};


class ClientMessage {
  public:
    uint64_t session_id;
    Direction turn_direction;
    uint32_t next_expected_event_no;
    char player_name[21];

    /// filling struct data with bytes from buffer
    void deserialize(char *buffer, int n) {
        if (n < 13 || n > 33)
            throw DeserializationException();

        session_id = be64toh(*(uint64_t *) buffer);
        turn_direction = *(Direction *) (buffer + 8);
        next_expected_event_no = ntohl(*(uint32_t *) (buffer + 9));
        for (int i = 0; i < n - 13; i++) {
            char c = buffer[i + 13];
            if (c < 33 || c > 126)
                throw DeserializationException();
            player_name[i] = c;
        }
        player_name[n - 13] = '\0';
    }
};


#endif //SIK_ROBAKI_MESSAGE_H

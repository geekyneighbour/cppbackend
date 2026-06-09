#pragma once
#include <cassert>
#include <optional>
#include <stdexcept>

class TV {
public:
    constexpr static int MIN_CHANNEL = 1;
    constexpr static int MAX_CHANNEL = 99;


    [[nodiscard]] bool IsTurnedOn() const noexcept {
        return is_turned_on_;
    }


    [[nodiscard]] std::optional<int> GetChannel() const noexcept {
        return is_turned_on_ ? std::optional{channel_} : std::nullopt;
    }


    void TurnOn() noexcept {
        is_turned_on_ = true;
    }

    /*
     * Выключает телевизор, если он был включен. Если телевизор уже выключен, ничего не делает.
     */
    void TurnOff() noexcept {
        is_turned_on_ = false;
    }


    void SelectChannel(int channel) {
        if (!is_turned_on_) {
            throw std::logic_error("TV is turned off");
        }
        if (channel < MIN_CHANNEL || channel > MAX_CHANNEL) {
            throw std::out_of_range("Channel is out of range");
        }
        if (channel_ == channel) {
            return;
        }
        previous_channel_ = channel_;
        channel_ = channel;
    }


    void SelectLastViewedChannel() {
        if (!is_turned_on_) {
            throw std::logic_error("TV is turned off");
        }

        int target_channel = previous_channel_;
        previous_channel_ = channel_;
        channel_ = target_channel;
    }

private:
    bool is_turned_on_ = false;
    int channel_ = 1;
    int previous_channel_ = 1; 
};
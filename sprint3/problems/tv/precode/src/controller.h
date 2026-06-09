#pragma once
#include <cassert>
#include <string_view>
#include <iostream>
#include <string>

#include "menu.h"
#include "tv.h"

class Controller {
public:
    Controller(TV& tv, Menu& menu)
        : tv_{tv}
        , menu_{menu} {
        using namespace std::literals;
        menu_.AddAction(std::string{INFO_COMMAND}, {}, "Prints info about the TV"s,
                        [this](auto& input, auto& output) {
                            return ShowInfo(input, output);
                        });
        menu_.AddAction(std::string{TURN_ON_COMMAND}, {}, "Turns on the TV"s,
                        [this](auto& input, auto& output) {
                            return TurnOn(input, output);
                        });
        menu_.AddAction(std::string{TURN_OFF_COMMAND}, {}, "Turns off the TV"s,
                        [this](auto& input, auto& output) {
                            return TurnOff(input, output);
                        });
        menu_.AddAction(std::string{SELECT_CHANNEL_COMMAND}, "CHANNEL"s,
                        "Selects the specified channel"s, [this](auto& input, auto& output) {
                            return SelectChannel(input, output);
                        });
        menu_.AddAction(std::string{SELECT_PREVIOUS_CHANNEL_COMMAND}, {},
                        "Selects the previously selected channel"s,
                        [this](auto& input, auto& output) {
                            return SelectPreviousChannel(input, output);
                        });
    }

private:
  
    [[nodiscard]] bool ShowInfo(std::istream& input, std::ostream& output) const {
        using namespace std::literals;

        if (EnsureNoArgsInInput(INFO_COMMAND, input, output)) {
            if (tv_.IsTurnedOn()) {
                output << "TV is turned on"sv << std::endl;
                output << "Channel number is "sv << tv_.GetChannel().value_or(0) << std::endl;
            } else {
                output << "TV is turned off"sv << std::endl;
            }
        }

        return true;
    }

    [[nodiscard]] bool TurnOn(std::istream& input, std::ostream& output) const {
        if (EnsureNoArgsInInput(TURN_ON_COMMAND, input, output)) {
            tv_.TurnOn();
        }
        return true;
    }

    [[nodiscard]] bool TurnOff(std::istream& input, std::ostream& output) const {
        if (EnsureNoArgsInInput(TURN_OFF_COMMAND, input, output)) {
            tv_.TurnOff();
        }
        return true;
    }


    [[nodiscard]] bool SelectChannel(std::istream& input, std::ostream& output) const {
        using namespace std::literals;
        
        int channel = 0;

        if (!(input >> channel)) {
            output << "Invalid channel"sv << std::endl;
            return true;
        }


        std::string extra;
        if (input >> extra) {
            output << "Invalid channel"sv << std::endl;
            return true;
        }

        try {
            tv_.SelectChannel(channel);
        } catch (const std::out_of_range&) {
            output << "Channel is out of range"sv << std::endl;
        } catch (const std::logic_error&) {
            output << "TV is turned off"sv << std::endl;
        }

        return true;
    }


    [[nodiscard]] bool SelectPreviousChannel(std::istream& input, std::ostream& output) const {
        using namespace std::literals;

        if (EnsureNoArgsInInput(SELECT_PREVIOUS_CHANNEL_COMMAND, input, output)) {
            try {
                tv_.SelectLastViewedChannel();
            } catch (const std::logic_error&) {
                output << "TV is turned off"sv << std::endl;
            }
        }
        return true;
    }

    [[nodiscard]] bool EnsureNoArgsInInput(std::string_view command, std::istream& input,
                                           std::ostream& output) const {
        using namespace std::literals;
        assert(input);
        if (std::string data; input >> data) {
            output << "Error: the " << command << " command does not require any arguments"sv
                   << std::endl;
            return false;
        }
        return true;
    }

    constexpr static std::string_view INFO_COMMAND = "Info";
    constexpr static std::string_view TURN_ON_COMMAND = "TurnOn";
    constexpr static std::string_view TURN_OFF_COMMAND = "TurnOff";
    constexpr static std::string_view SELECT_CHANNEL_COMMAND = "SelectChannel";
    constexpr static std::string_view SELECT_PREVIOUS_CHANNEL_COMMAND = "SelectPreviousChannel";

    TV& tv_;
    Menu& menu_;
};
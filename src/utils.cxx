module;
#include <cassert>
#include <string>

export module utils;

export namespace Utils {

    // Replace with std::expected in C++23
    template<typename S>
    class May {
        S val;
        std::string msg;
        bool has_val;

    public:
        May(S val): val(val), has_val(true) {}
        May(std::string msg): msg(msg), has_val(false) {}

        bool has_value() const {
            return has_val;
        }
        S& value() {
            assert(has_val);
            return val;
        }
        std::string& error() {
            assert(!has_val);
            return msg;
        }

        bool operator!() const {
            return !has_value();
        }
    };

    // an expected with a ok result
    May<bool> expected() {
        return May(true);
    }

    template<typename S>
    May<S> expected(S& val) {
        return May(val);
    }
    template<typename S>
    May<S> expected(S val) {
        return May(val);
    }

    template<typename S>
    May<S> unexpected(std::string str) {
        return May<S>(str);
    }

};

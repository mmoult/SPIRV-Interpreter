module;
#include <cassert>
#include <string>

export module utils;

export namespace Utils {

    // Replace with std::expected in C++23
    template<typename S>
    class May {
        S* val;
        std::string msg;

    public:
        May(S* val, std::string msg): val(val), msg(msg) {}

        bool has_value() const {
            return val != nullptr;
        }
        S& value() {
            assert(val != nullptr);
            return *val;
        }
        std::string& error() {
            assert(val == nullptr);
            return msg;
        }

        bool operator!() const {
            return !has_value();
        }

        /// @brief Deletes the data stored in the May
        /// May does *not* own the data, but if you want to delete it, this will and turn itself into unexpected
        void del() {
            assert(val != nullptr);
            delete val;
            val = nullptr;
            msg = "deleted";
        }
    };

    const bool silent = true;
    // an expected with a silent/none ok result
    May<const bool> expected() {
        return May(&silent, "");
    }

    template<typename S>
    May<S> expected(S& val) {
        return May(&val, "");
    }
    template<typename S>
    May<S> unexpected(std::string str) {
        return May<S>(nullptr, str);
    }

};

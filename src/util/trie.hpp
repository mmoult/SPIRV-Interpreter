/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef UTIL_TRIE_HPP
#define UTIL_TRIE_HPP

#include <sstream>
#include <string>
#include <tuple>
#include <vector>

class Trie {
    std::string key;
    std::vector<Trie> children;
    bool valued;
    unsigned value;

    Trie(const std::string& key, bool valued, unsigned value = 0): key(key), valued(valued), value(value) {}

    inline void reset(const std::string& key, bool valued, unsigned value = 0) {
        this->key = key;
        children.clear();
        this->valued = valued;
        this->value = value;
    }

    /// @brief Find the index where the key belongs
    /// @param key the first character of the key to locate
    /// @return <index in children vector, exact match>
    std::tuple<unsigned, bool> index(char key) const;

    void enumerate(const std::string& prefix, std::vector<std::string>& options) const;

    void toString(
        unsigned& id,
        unsigned parent,
        std::stringstream& properties,
        std::stringstream& connections
    ) const;

public:
    Trie(bool valued = false, unsigned value = 0): valued(valued), value(value) {}

    inline void clear() {
        reset(key, false);
    }

    /// @brief Insert the key and associated value into the trie
    /// @param key the key which identifies the value
    /// @param value the value of the key
    /// @return the trie created / overwritten with the value
    Trie& insert(const std::string& key, unsigned value);

    /// @brief Try to find the trie node, the key for which, is formed from the search key plus some amount
    ///        (including none). This can best be conceptualized as the trie which the key is an abbreviation for.
    /// @param key the key to find the trie node for
    /// @return <trie found, where null may be returned (in which case, the key exceeds all nodes), string missing>
    std::tuple<const Trie*, std::string> next(const std::string& key) const;

    inline bool hasValue() const {
        return valued;
    }
    inline unsigned getValue() const {
        return value;
    }

    /// @brief Return a list of all expanded children's keys
    /// @return the list
    inline std::vector<std::string> enumerate() const {
        std::vector<std::string> options;
        enumerate("", options);
        return options;
    }

    /// @brief Print the trie in graphviz-readable syntax
    /// @return a string which represents the trie
    std::string toString() const;
};
#endif

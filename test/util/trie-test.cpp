/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <iostream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "../../src/util/trie.hpp"

std::vector<std::string> keys{
    "addon",
    "address",
    "app",
    "break",
    "breakpoint",
    "breakpoint add",
    "breakpoint clear",
    "breakpoint remove",
    "continue",
    "crash",
    "program",
    "stack",
    "tear",
    "tearing",
    "tears",
};

bool addValue(Trie& trie, unsigned which) {
    if (which < keys.size()) {
        trie.insert(keys[which], which);
        return true;
    }
    return false;
}

void addValues(Trie& trie, std::vector<unsigned>& order) {
    std::vector<bool> complete(keys.size(), false);

    for (unsigned o : order) {
        if (addValue(trie, o))
            complete[o] = true;
    }

    // Fill in missing entries
    for (unsigned i = 0; i < complete.size(); ++i) {
        if (!complete[i])
            addValue(trie, i);
    }
}

TEST_CASE("insertion_tests", "[trie]") {
    for (unsigned i = 0; i < 4; ++i) {
        Trie trie;
        std::vector<unsigned> order;
        order.reserve(keys.size());
        switch (i) {
        case 0: // default ordering case
            break;
        case 1: {
            // populate "breakpoint" with children, then replace it in the root trie with "break"
            const unsigned breakpoint = 4;
            order.push_back(breakpoint);
            order.push_back(breakpoint + 1);
            order.push_back(breakpoint + 2);
            order.push_back(breakpoint - 1);
            // afterward, add "breakpoint remove", which should be directed as a child of the first
            break;
        }
        case 2: {
            // find common (not key) entry between tearing and tears, then make it a key with "tear"
            const unsigned tearing = 13;
            order.push_back(tearing);
            order.push_back(tearing + 1);
            order.push_back(tearing - 1);
            break;
        }
        case 3: {
            // Repeated indices in order (insert again should overwrite without error)
            order.push_back(4);
            order.push_back(1);
            order.push_back(11);
            order.push_back(12);
            order.push_back(12);
            order.push_back(5);
            order.push_back(0);
            order.push_back(1);
            break;
        }
        default: // all cases should be specified!
            REQUIRE(false);
        }
        addValues(trie, order);
        if (trie.enumerate() != keys) {
            std::cout << "insertion_test Trie " << i << "=" << std::endl;
            std::cout << trie.toString() << std::endl;
        }
        // All keys should have been preserved and in alphabetical order
        CHECK(trie.enumerate() == keys);
    }
}

void check(Trie& root, std::string search, std::string expected, unsigned val) {
    auto [t, rem] = root.next(search);
    CHECK(rem == expected);
    REQUIRE(t != nullptr);
    REQUIRE(t->hasValue());
    REQUIRE(t->getValue() == val);
}

TEST_CASE("next", "[trie]") {
    Trie trie;
    std::vector<unsigned> order;
    addValues(trie, order);

    SECTION("1 letter") {
        check(trie, "p", "rogram", 10);
    }
    SECTION("several letters") {
        check(trie, "cont", "inue", 8);
    }
    SECTION("full match") {
        check(trie, "stack", "", 11);
    }
    SECTION("exceed key") {
        auto [t, rem] = trie.next("applet");
        REQUIRE(t == nullptr);
    }
    SECTION("assume midpoint") {
        check(trie, "br", "eak", 3);
    }
    SECTION("ambiguous exact match") {
        auto [t, rem] = trie.next("c");
        CHECK(rem == "");
        REQUIRE(t != nullptr);
        CHECK(!t->hasValue());
    }
    SECTION("ambiguous midpoint") {
        auto [t, rem] = trie.next("ad");
        CHECK(rem == "d");
        REQUIRE(t != nullptr);
        CHECK(!t->hasValue());
    }
}

TEST_CASE("enumerate", "[trie]") {
    Trie trie;
    std::vector<unsigned> order{1, 3, 5, 13, 12, 11};
    addValues(trie, order);

    auto [t, missing] = trie.next("breakpoint ");
    REQUIRE(t != nullptr);
    // Enumerate should not repeat prior or the current key
    CHECK(t->enumerate() == std::vector<std::string>{"add", "clear", "remove"});
}

/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "trie.hpp"

#include <algorithm>  // for min

std::tuple<unsigned, bool> Trie::index(char key) const {
    // Do a binary search between children indices
    unsigned min = 0;
    unsigned max = children.size();
    while (min < max) {
        unsigned i = min + (max - min) / 2;  // intentionally floor since max may be oob
        char ref = children[i].key[0];
        if (key > ref)
            min = i + 1;
        else if (key < ref)
            max = i;  // insert pushes back current index, so i might be needed
        else
            // Otherwise, we have an exact match
            return std::tuple(i, true);
    }
    return std::tuple(min, false);
}

void Trie::enumerate(const std::string& prefix, std::vector<std::string>& options) const {
    if (valued)
        options.push_back(prefix);
    for (const Trie& kid : children)
        kid.enumerate(prefix + kid.key, options);
}

void
Trie::toString(unsigned& id, unsigned parent, std::stringstream& properties, std::stringstream& connections) const {
    unsigned myid = id++;
    if (parent < myid) {
        // don't print a connection from null!
        connections << parent << " -> " << myid << " [label=\"" << key << "\"]\n";
    }
    properties << myid << " [label=\"";
    if (valued)
        properties << value;
    properties << "\"]\n";
    for (const Trie& kid : children)
        kid.toString(id, myid, properties, connections);
}

Trie& Trie::insert(const std::string& key, unsigned value) {
    if (key.empty()) {
        this->value = value;
        this->valued = true;
        return *this;
    }
    auto [at, exact] = index(key[0]);
    if (!exact)
        return *children.insert(children.begin() + at, Trie(key, true, value));
    else {
        Trie& other = children[at];
        // We know that key and other key have a common prefix (>= 1 char). How far does the commonality extend?
        unsigned common = 1;
        unsigned klen = key.length();
        unsigned olen = other.key.length();
        unsigned min_length = std::min(klen, olen);
        for (; common < min_length; ++common) {
            if (key[common] != other.key[common])
                break;
        }
        if (common == olen) {
            // the new node can become a child of the other node
            return other.insert(key.substr(common), value);
        } else {
            Trie copy = other;
            copy.key = copy.key.substr(common);
            if (common == klen) {
                // the new node replaces other and other becomes a child of it
                other.reset(key, true, value);
                other.children.push_back(copy);
                return other;
            } else {
                // We must create a third node which gets new and other as kids
                other.reset(key.substr(0, common), false, 0);
                auto substr = key.substr(common);
                other.children.push_back(copy);
                return *other.children.insert(
                    other.children.begin() + (substr < copy.key ? 0 : 1),
                    Trie(substr, true, value)
                );
            }
        }
    }
}

std::tuple<const Trie*, std::string> Trie::next(const std::string& key) const {
    if (key.empty())
        return std::tuple(this, "");
    auto [at, exact] = index(key[0]);
    if (!exact)
        return std::tuple(nullptr, "");
    const Trie& other = children[at];
    // We know that key and other key have a common prefix (>= 1 char). How far does the commonality extend?
    unsigned common = 1;
    unsigned klen = key.length();
    unsigned olen = other.key.length();
    unsigned min_length = std::min(klen, olen);
    for (; common < min_length; ++common) {
        if (key[common] != other.key[common])
            break;
    }
    if (common == klen)
        return std::tuple(&other, other.key.substr(common));
    if (common == olen)
        return other.next(key.substr(common));
    return std::tuple(nullptr, "");
}

std::string Trie::toString() const {
    std::stringstream properties;
    std::stringstream connections;
    properties << "digraph D {\n{\n";
    unsigned id = 0;
    toString(id, 0, properties, connections);
    properties << "}\n" << connections.str() << "}";
    return properties.str();
}

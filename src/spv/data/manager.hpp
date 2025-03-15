/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_DATA_MANAGER_HPP
#define SPV_DATA_MANAGER_HPP

#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

import spv.data.data;

class DataManager;

class DataView {
    // src must not be null. Only pointer (instead of reference) to prevent circular dependency
    DataManager* src;
    DataView* prev = nullptr;
    std::map<unsigned, Data> data;

public:
    DataView(DataManager* src) : src(src) {};
    DataView(const DataView& view) = delete;
    DataView& operator=(const DataView& other) = delete;
    ~DataView() {
        // Must clear out all data
        for (auto [index, dat] : data) {
            // Nice to keep the index for debugging purposes (ie if we double-delete something, the index will say what)
            dat.clear();
        }
    }

    Data& at(unsigned index);
    const Data& at(unsigned index) const;

    Data& operator[](unsigned index);
    const Data& operator[](unsigned index) const;

    Data& local(unsigned index) {
        return data[index];
    }

    bool contains(unsigned index) const;

    unsigned getBound() const;

    inline void setSource(DataManager* src) {
        this->src = src;
    }
    inline DataManager* getSource() const {
        return src;
    }

    inline void setPrev(DataView* view) {
        this->prev = view;
    }
    inline DataView* getPrev() const {
        return prev;
    }

    DataView* clone() const;
};

class DataManager {
    unsigned max;
    DataView global;
    std::vector<DataView*> views;

public:
    DataManager(unsigned max = 0) : max(max), global(nullptr) {
        global.setSource(this);
    };
    DataManager(const DataManager& other) = delete;
    DataManager& operator=(const DataManager& other) = delete;
    ~DataManager() {
        for (unsigned i = 0; i < views.size(); ++i)
            delete views[i];
        views.clear();
    }

    inline DataView& getGlobal() {
        return global;
    }
    inline const DataView& getGlobal() const {
        return global;
    }

    inline unsigned getBound() {
        return max;
    }
    inline void setBound(unsigned max) {
        this->max = max;
    }

    inline DataView* makeView(DataView* prev = nullptr) {
        auto view = new DataView(this);
        view->setPrev(prev);
        views.push_back(view);
        return view;
    }

    inline void destroyView(DataView* view) {
        auto it = std::find(views.begin(), views.end(), view);
        if (it == views.end())
            throw std::runtime_error("Could not delete view not present in manager!");
        views.erase(it);
        delete view;
    }
};
#endif

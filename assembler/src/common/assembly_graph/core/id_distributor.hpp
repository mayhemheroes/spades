//***************************************************************************
//* Copyright (c) 2018 Saint Petersburg State University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#pragma once

#include "adt/iterator_range.hpp"
#include <boost/iterator/iterator_facade.hpp>

#include <vector>
#include <cstddef>
#include <cstdint>

namespace omnigraph {

class ReclaimingIdDistributor {
  public:
    ReclaimingIdDistributor(uint64_t bias = 0, size_t initial_size = 1)
            : bias_(bias) {
        resize(initial_size);
    }

    void resize(size_t sz);
    uint64_t allocate(uint64_t offset = 0);
    size_t free() const;
    size_t size() const {
        return free_map_.size();
    }
    bool occupied(uint64_t at) const {
        return !free_map_[at - bias_];
    }
    void acquire(uint64_t at) {
        // FIXME: "lock" only single bit
#pragma omp critical
        free_map_[at - bias_] = false;
    }
    void release(uint64_t at) {
        // FIXME: "lock" only single bit
#pragma omp critical
        free_map_[at - bias_] = true;
    }

    class id_iterator : public boost::iterator_facade<id_iterator,
                                                      uint64_t,
                                                      boost::forward_traversal_tag,
                                                      uint64_t> {
      public:
        id_iterator(uint64_t start,
                    const std::vector<bool> &map,
                    uint64_t bias)
                : map_(map), bias_(bias), cur_(start) {
            if (cur_ != -1ULL && map_[cur_])
                cur_ = next_occupied(cur_);
        }

      private:
        friend class boost::iterator_core_access;

        uint64_t dereference() const {
            return cur_ + bias_;
        }

        uint64_t next_occupied(uint64_t n) const {
            for (size_t i = n + 1; i < map_.size(); ++i) {
                if (!map_[i])
                    return i;
            }

            return map_.size();
        }

        void increment() {
            if (cur_ == -1ULL)
                return;

            cur_ = next_occupied(cur_);
            if (cur_ == map_.size())
                cur_ = -1ULL;
        }

        bool equal(const id_iterator &other) const {
            return cur_ == other.cur_;
        }

      private:
        const std::vector<bool> &map_;
        uint64_t bias_, cur_;
    };

    id_iterator begin() const {
        return id_iterator(0, free_map_, bias_);
    }
    id_iterator end() const {
        return id_iterator(-1ULL, free_map_, bias_);
    }
    adt::iterator_range<id_iterator> ids() const {
        return adt::make_range(begin(), end());
    }


  private:
    friend class id_iterator;

    uint64_t next_free(uint64_t n = 0) const;

    uint64_t last_allocated_;
    uint64_t bias_;
    std::vector<bool> free_map_;
};

}
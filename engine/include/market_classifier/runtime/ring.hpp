#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace market_classifier::runtime {

// Fixed-capacity ring buffer that overwrites the oldest element. Storage lives on the heap
// and grows only up to N, so large rings (heatmap, 24h series) do not bloat owners.
// Index 0 is the oldest retained element.
template <typename T, std::size_t N> class Ring {
    static_assert(N > 0, "Ring capacity must be positive");

  public:
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

    void push(T value) {
        if (items_.size() < N) {
            items_.push_back(std::move(value));
            return;
        }
        items_[head_] = std::move(value);
        head_         = (head_ + 1) % N;
        ++overwritten_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

    [[nodiscard]] const T &operator[](std::size_t i) const {
        return items_[(head_ + i) % items_.size()];
    }
    [[nodiscard]] T &operator[](std::size_t i) { return items_[(head_ + i) % items_.size()]; }

    // Newest element; precondition: !empty().
    [[nodiscard]] const T &back() const { return (*this)[items_.size() - 1]; }
    [[nodiscard]] T &back() { return (*this)[items_.size() - 1]; }

    // Lifetime count of elements evicted by capacity (not reset by clear()).
    [[nodiscard]] std::uint64_t overwritten() const noexcept { return overwritten_; }

    void clear() noexcept {
        items_.clear();
        head_ = 0;
    }

  private:
    std::vector<T> items_;
    std::size_t head_          = 0; // index of the oldest element once full
    std::uint64_t overwritten_ = 0;
};

} // namespace market_classifier::runtime

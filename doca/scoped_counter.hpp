#pragma once

namespace doca {
    template<typename T = int>
    class counter_guard {
    public:
        counter_guard(T *ptr = nullptr) noexcept:
            ptr_ { ptr }
        {
            if(ptr_) {
                ++*ptr_;
            }
        }

        counter_guard(counter_guard &&other) noexcept {
            swap(other);
        }

        counter_guard &operator=(counter_guard &&other) noexcept {
            auto tmp = counter_guard(std::move(other));
            swap(tmp);
        }

        ~counter_guard() {
            if(ptr_ != nullptr) {
                --*ptr_;
            }
        }

        auto swap(counter_guard &other) noexcept -> void{
            std::swap(ptr_, other.ptr_);
        }
    
    private:

        T *ptr_ = nullptr;
    };

    template<typename T = int>
    class scoped_counter {
    public:
        scoped_counter(T val = {}):
            counter_ { val }
        {
        }

        auto guard() -> counter_guard<T> {
            return { &counter_ };
        }

        auto value() const {
            return counter_;
        }

    private:
        int counter_ = 0;
    };
}

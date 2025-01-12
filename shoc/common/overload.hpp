#pragma once

namespace shoc {
    // for convenient std::variant visiting
    template<typename... Fs> struct overload : Fs... { using Fs::operator()...; };
    template<typename... Fs> overload(Fs...) -> overload<Fs...>;
}

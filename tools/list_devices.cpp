#include <shoc/shoc.hpp>

#include <cppcodec/hex_lower.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <array>
#include <iostream>
#include <string>

[[nodiscard]] auto display_content(
    doca_error_t err,
    std::string buffer
) -> std::string {
    return err == DOCA_SUCCESS ? buffer : doca_error_get_descr(err);
}

auto main() -> int {
    std::cout << "Devices\n";

    for(auto dev : shoc::device_list{}) try {
        char char_buf[1024] = "";
        std::array<std::uint8_t, DOCA_DEVINFO_MAC_ADDR_SIZE>  mac_buf;
        std::array<std::uint8_t, DOCA_DEVINFO_IPV4_ADDR_SIZE> ipv4_buf;
        std::array<std::uint8_t, DOCA_DEVINFO_IPV6_ADDR_SIZE> ipv6_buf;
        std::uint16_t lid;
        std::uint64_t active_rate;
        std::uint16_t vhca_id;

        doca_error_t err;

        err = doca_devinfo_get_pci_addr_str(dev, char_buf);
        auto pci_address = display_content(err, char_buf);

        err = doca_devinfo_get_iface_name(dev, char_buf, sizeof char_buf);
        auto iface_name = display_content(err, char_buf);

        err = doca_devinfo_get_ibdev_name(dev, char_buf, sizeof char_buf);
        auto ibdev_name = display_content(err, char_buf);

        err = doca_devinfo_get_mac_addr(dev, mac_buf.data(), mac_buf.size());
        auto mac_address = display_content(err, fmt::format("{:x}", fmt::join(mac_buf, ":")));

        err = doca_devinfo_get_ipv4_addr(dev, ipv4_buf.data(), ipv4_buf.size());
        auto ipv4_address = display_content(err, fmt::format("{:d}", fmt::join(ipv4_buf, ".")));

        err = doca_devinfo_get_ipv4_addr(dev, ipv6_buf.data(), ipv6_buf.size());
        auto ipv6_address = display_content(err, cppcodec::hex_lower::encode(ipv6_buf));

        err = doca_devinfo_get_lid(dev, &lid);
        auto rendered_lid = display_content(err, fmt::format("{:x}", lid));

        err = doca_devinfo_get_active_rate(dev, &active_rate);
        auto rendered_ar = display_content(err, fmt::format("{:d}", active_rate));

        err = doca_devinfo_get_vhca_id(dev, &vhca_id);
        auto rendered_vhca = display_content(err, fmt::format("{:x}", vhca_id));

        std::cout << 
            "---\n"
            "PCI:   " << pci_address   << "\n"
            "Iface: " << iface_name    << "\n"
            "IBDev: " << ibdev_name    << "\n"
            "MAC:   " << mac_address   << "\n"
            "IPv4:  " << ipv4_address  << "\n"
            "IPv6:  " << ipv6_address  << "\n"
            "LID:   " << rendered_lid  << "\n"
            "VHCA:  " << rendered_vhca << "\n"
            "ARate: " << rendered_ar   << " bits/s\n"
            ;

#ifdef DOCA_ARCH_DPU

        auto shocdev = shoc::device{dev};
        std::cout <<
            "Representors\n"
            "    ---\n";

        for(auto rep : shoc::device_rep_list{shocdev}) {
            char char_buf[1024] = "";
            doca_pci_func_type func_type;
            std::uint8_t is_hotplug;
            std::uint16_t vhca_id;
            doca_error_t err;

            err = doca_devinfo_rep_get_vuid(rep, char_buf, sizeof char_buf);
            auto vuid = display_content(err, char_buf);

            err = doca_devinfo_rep_get_pci_addr_str(rep, char_buf);
            auto pci_address = display_content(err, char_buf);

            char const *type_names[] = { "PF", "VF", "SF" };
            err = doca_devinfo_rep_get_pci_func_type(rep, &func_type);
            auto rendered_ftype = display_content(err, type_names[func_type]);

            err = doca_devinfo_rep_get_is_hotplug(rep, &is_hotplug);
            auto rendered_hotplug = display_content(err, fmt::format("{:d}", is_hotplug));

            err = doca_devinfo_rep_get_iface_name(rep, char_buf, sizeof char_buf);
            auto iface_name = display_content(err, char_buf);

            err = doca_devinfo_rep_get_vhca_id(rep, &vhca_id);
            auto rendered_vhca = display_content(err, fmt::format("{:d}", vhca_id));

            std::cout <<
                "    PCI:   " << pci_address << "\n"
                "    Iface: " << iface_name << "\n"
                "    VUID:  " << vuid << "\n"
                "    Type:  " << rendered_ftype << "\n"
                "    Hotp:  " << rendered_hotplug << "\n"
                "    ---\n";
        }

#endif

    } catch(shoc::doca_exception &e) {
        shoc::logger->warn("Error when reading device: {}", e.what());
    }
}

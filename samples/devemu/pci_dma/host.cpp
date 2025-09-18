#include <cxxopts.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <fcntl.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>

static constexpr std::size_t MEM_BUF_LEN = 4096;

std::shared_ptr<spdlog::logger> const logger = spdlog::stderr_color_st("devemu_host_sample");

template<typename... Args>
[[noreturn]]
auto throw_error(fmt::format_string<Args...> fmt, Args&& ...args) -> void {
    auto message = fmt::format(fmt, std::forward<Args>(args)...);
    logger->error(message);
    throw std::runtime_error(message);
}

class file_descriptor {
public:
    static int constexpr invalid = -1;

    file_descriptor() = default;
    file_descriptor(int fd):
        fd_ { fd }
    {}

    file_descriptor(file_descriptor const &) = delete;
    file_descriptor(file_descriptor &&other):
        fd_ { std::exchange(other.fd_, invalid) }
    { }

    file_descriptor &operator=(file_descriptor const &) = delete;
    file_descriptor &operator=(file_descriptor &&other) {
        close();
        fd_ = std::exchange(other.fd_, invalid);
        return *this;
    }

    ~file_descriptor() {
        close();
    }

    [[nodiscard]] auto value() const -> int {
        return fd_;
    }

    [[nodiscard]] auto ptr() const -> int const * {
        return &fd_;
    }

    [[nodiscard]] auto is_valid() const -> bool {
        return fd_ >= 0;
    }

    auto assign(int new_fd) -> void {
        close();
        fd_ = new_fd;
    }

    auto close() -> void {
        if(is_valid()) {
            ::close(fd_);
            fd_ = invalid;
        }
    }

private:
    int fd_ = invalid;
};

class vfio_dma_region {
public:
    struct unmapper {
        std::size_t size_;

        auto operator()(void *base_ptr) const {
            if(base_ptr != MAP_FAILED) {
                if(munmap(base_ptr, size_) != 0) {
                    logger->error("munmap failed: {}", strerror(errno));
                }
            }
        }
    };

    vfio_dma_region(
        int container_fd,
        std::size_t size,
        std::uint64_t iova
    ):
        base_ptr_ { 
            ::mmap(
                nullptr,
                size,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0
            ),
            unmapper(size)
        },
        size_ { size },
        iova_ { iova },
        container_fd_ { container_fd }
    {
        if(base_ptr_.get() == MAP_FAILED) {
            throw_error("mmap failed: {}", strerror(errno));
        }

        vfio_iommu_type1_dma_map dma_map = {};
        dma_map.argsz = sizeof(vfio_iommu_type1_dma_map);
        dma_map.vaddr = reinterpret_cast<std::uint64_t>(base_ptr_.get());
        dma_map.size = size_;
        dma_map.iova = iova;
        dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

        if(auto status = ioctl(container_fd_, VFIO_IOMMU_MAP_DMA, &dma_map); status != 0) {
            throw_error("Failed to VFIO_IOMMU_MAP_DMA, status = {}, error = {}", status, strerror(errno));
        }
    }

    ~vfio_dma_region() {
        vfio_iommu_type1_dma_map dma_unmap = {};
        dma_unmap.argsz = sizeof(vfio_iommu_type1_dma_map);
        dma_unmap.iova = iova_;
        dma_unmap.size = size_;

        if(auto status = ioctl(container_fd_, VFIO_IOMMU_UNMAP_DMA, &dma_unmap); status != 0) {
            logger->error("Failed to VFIO_IOMMU_UNMAP_DMA, status = {}, error = {}", status, strerror(errno));
        }
    }

    auto as_chars() -> std::span<char> {
        return { static_cast<char*>(base_ptr_.get()), size_ };
    }

private:
    std::unique_ptr<void, unmapper> base_ptr_;
    std::size_t size_;
    std::uint64_t iova_;
    int container_fd_;
};

auto validate_vfio_group_and_container(
    file_descriptor &group_fd,
    file_descriptor &container_fd
) -> void {
    auto vfio_api_version = ioctl(container_fd.value(), VFIO_GET_API_VERSION);
    if(vfio_api_version != VFIO_API_VERSION) {
        throw_error("VFIO API version mismatch. compiled with {}, but runtime is {}", VFIO_API_VERSION, vfio_api_version);
    }

    if(ioctl(container_fd.value(), VFIO_CHECK_EXTENSION, VFIO_TYPE1v2_IOMMU) != 0) {
        throw_error("VFIO Type 1 IOMMU extension not supported");
    }

    auto group_status = vfio_group_status { };
    group_status.argsz = sizeof(vfio_group_status);

    if(auto status = ioctl(group_fd.value(), VFIO_GROUP_GET_STATUS, &group_status); status != 0) {
        throw_error("Failed to get status of VFIO group. Status = {}, error = {}", status, strerror(errno));
    }

    if((group_status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0) {
        throw_error("VFIO group not viable. Not all devices in IOMMU group are bound to vfio driver");
    }
}

auto add_vfio_group_to_container(
    file_descriptor &group_fd,
    file_descriptor &container_fd
) -> void {
    if(auto status = ioctl(group_fd.value(), VFIO_GROUP_SET_CONTAINER, container_fd.ptr()); status != 0) {
        throw_error("Failed to set group for container. Status = {}, error = {}", status, strerror(errno));
    }

    if(auto status = ioctl(container_fd.value(), VFIO_SET_IOMMU, VFIO_TYPE1v2_IOMMU); status != 0) {
        throw_error("Failed to set IOMMU type 1 extension for container. Status = {}, error = {}", status, strerror(errno));
    }
}

auto init_vfio_device(
    int vfio_group,
    std::string const &pci_address
) ->
    std::tuple<
        file_descriptor,
        file_descriptor,
        file_descriptor
    >
{
    auto container_fd = file_descriptor { open("/dev/vfio/vfio", O_RDWR) };

    if(!container_fd.is_valid()) {
        throw_error("Failed to open VFIO container. error = {}", strerror(errno));
    }

    auto group_fd = file_descriptor { open(fmt::format("/dev/vfio/{}", vfio_group).c_str(), O_RDWR) };

    if(!group_fd.is_valid()) {
        throw_error("Failed to open VFIO group. error = {}", strerror(errno));
    }

    validate_vfio_group_and_container(group_fd, container_fd);
    add_vfio_group_to_container(group_fd, container_fd);

    auto device_fd = file_descriptor { ioctl(group_fd.value(), VFIO_GROUP_GET_DEVICE_FD, pci_address.c_str()) };
    if(!device_fd.is_valid()) {
        throw_error("Failed to get device fd, error = {}", strerror(errno));
    }

    auto reg = vfio_region_info { };
    reg.argsz = sizeof(vfio_region_info);
    reg.index = VFIO_PCI_CONFIG_REGION_INDEX;

    if(auto status = ioctl(device_fd.value(), VFIO_DEVICE_GET_REGION_INFO, &reg); status != 0) {
        throw_error("Failed to get Config Region info. Status = {}, error = {}", status, strerror(errno));
    }

    auto cmd = std::uint16_t { 0x6 };
    if(pwrite(device_fd.value(), &cmd, 2, reg.offset + 0x4) != 2) {
        throw_error("Failed to enable PCI cmd. Failed to write to Config Region Space. Error = {}", strerror(errno));
    }

    return {
        std::move(container_fd),
        std::move(group_fd),
        std::move(device_fd)
    };
}

auto devemu_dma_demo_host(
    std::string pci_address,
    std::uint64_t iova,
    int vfio_group,
    std::string write_data
) -> void {
    auto [ container_fd, group_fd, device_fd ] = init_vfio_device(vfio_group, pci_address);

    logger->info("obtained VFIO group and device");

    auto dma_mem = vfio_dma_region { container_fd.value(), MEM_BUF_LEN, iova };

    logger->info("mapped device memory");

    write_data.resize(MEM_BUF_LEN, '\0');
    std::ranges::copy(write_data, dma_mem.as_chars().data());

    logger->info("Wrote data to device");

    while(std::ranges::equal(dma_mem.as_chars(), write_data)) {
        using namespace std::chrono_literals;
        logger->info("Waiting for new data from device");
        std::this_thread::sleep_for(1s);
    }

    logger->info("Got new data from device");
}

auto main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> int try {
    logger->set_level(spdlog::level::debug);

    auto options = cxxopts::Options("shoc-devemu-pci-dma", "PCI device emulation: DMA demo program");

    options.add_options()
        (
            "p,pci-addr",
            "PCI address of the emulated device",
            cxxopts::value<std::string>()->default_value("e3:00.0")
        )
        (
            "a,addr",
            "Host DMA memory IOVA address",
            cxxopts::value<std::uint64_t>()->default_value("0x1000000")
        )
        (
            "g,vfio-group",
            "VFIO group of the device (integer)",
            cxxopts::value<int>()->default_value("-1")
        )
        (
            "w,write-data",
            "Data to write to the host memory",
            cxxopts::value<std::string>()->default_value("This is a sample piece of data from DPU!")
        )
        ;

    auto cmdline = options.parse(argc, argv);

    auto pci_addr = cmdline["pci-addr"].as<std::string>();
    auto iova = cmdline["addr"].as<std::uint64_t>();
    auto vfio_group = cmdline["vfio-group"].as<int>();
    auto write_data = cmdline["write-data"].as<std::string>();

    devemu_dma_demo_host(
        pci_addr,
        iova,
        vfio_group,
        write_data
    );
} catch(std::exception &e) {
    logger->error(e.what());
}

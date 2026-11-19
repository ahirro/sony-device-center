#include "sony/core/DeviceService.h"
#include "sony/protocol/DeviceProfileRegistry.h"
#include "sony/transport/Logger.h"
#include <algorithm>
#include <cstdlib>

namespace sony::core {

DeviceService::DeviceService(
    std::shared_ptr<transport::ITransport> transport,
    std::shared_ptr<transport::IDeviceDiscovery> discovery, Now now)
    : _transport(std::move(transport)), _discovery(std::move(discovery)), _now(std::move(now)) {
    // SONY_IDLE_RELEASE_SECONDS=0 turns idle release off; default 15 s.
    if (const char* v = std::getenv("SONY_IDLE_RELEASE_SECONDS")) {
        char* end = nullptr;
        const long secs = std::strtol(v, &end, 10);
        if (end != v && secs >= 0 && secs <= 86400) _idleRelease = std::chrono::seconds(secs);
    }
    if (const char* v = std::getenv("SONY_IDLE_PEEK_SECONDS")) {
        char* end = nullptr;
        const long secs = std::strtol(v, &end, 10);
        if (end != v && secs >= 0 && secs <= 86400) _peekInterval = std::chrono::seconds(secs);
    }
}

DeviceService::~DeviceService() {
    disconnect();
}

std::vector<DiscoveredDevice> DeviceService::discoverDevices() {
    if (!_discovery) {
        return {};
    }
    auto rawDevices = _discovery->discover();
    std::vector<DiscoveredDevice> result;
    result.reserve(rawDevices.size());
    for (const auto& dev : rawDevices) {
        auto profile = protocol::DeviceProfileRegistry::getProfileForDevice(dev.name);
        result.push_back(DiscoveredDevice{
            .address = dev.address.str(),
            .name = dev.name,
            .version = profile.protocol,
            .paired = dev.paired, .connected = dev.connected
        });
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.connected.value_or(false) != b.connected.value_or(false)) return a.connected.value_or(false);
        return a.address < b.address;
    });
    return result;
}

void DeviceService::connect(const transport::DeviceAddress& address, std::string_view name) {
    std::lock_guard lock(_mutex);
    _target = address.str(); _automatic = true; _retrySeconds = 1; _released = false;
    try { _connect(address, name); }
    catch (const std::exception& ex) {
        _wasConnected = false;
        if (_device) _device->disconnect();
        _lastError = ex.what(); _connectionState = "retrying";
        _nextAttempt = _now() + std::chrono::seconds(1); throw;
    }
}

void DeviceService::_connect(const transport::DeviceAddress& address, std::string_view name) {
    _selected = address.str(); _selectedName = std::string(name); _connectionState = "connecting";
    if (!_device) {
        // The version passed here is provisional; SonyDevice::connect() resolves
        // it from the device profile. Start from V1 so that a failure to resolve
        // can never leave a legacy device on the V2 command set.
        _device = std::make_unique<SonyDevice>(_transport, SonyProtocolVersion::V1);
    }
    _device->connect(address, name);
    if (!_device->isConnected()) throw SonyException(SonyErrorCode::Disconnected, "Bluetooth link closed during initialization");
    _wasConnected = true; _released = false; _lastActivity = _now();
    _connectionState = "connected"; _lastError.clear(); _retrySeconds = 1;
    _nextSettings = _now() + std::chrono::seconds(5);
    _nextBattery = _now() + std::chrono::seconds(30);
    Logger::info(LogCategory::Device, "Connected to " + std::string(name));
}

void DeviceService::disconnect() noexcept {
    std::lock_guard lock(_mutex);
    _automatic = false; _wasConnected = false; _released = false; _connectionState = "manually_disconnected";
    if (_device) {
        _device->disconnect();
    }
}

void DeviceService::cancelPending() noexcept {
    // Do not acquire the service mutex: tick/connect may hold it while waiting
    // for exactly the native operation being cancelled. Transport is immutable.
    if (_transport) _transport->cancelPending();
}

bool DeviceService::isConnected() const noexcept {
    std::lock_guard lock(_mutex);
    // An idle-released session still counts as connected: the link is only
    // parked so another client can use it, and wake() reopens it on demand.
    return _released || (_device && _device->isConnected());
}

SonyDevice* DeviceService::activeDevice() noexcept {
    std::lock_guard lock(_mutex);
    return _device.get();
}

protocol::DeviceStateSnapshot DeviceService::snapshot() const {
    std::lock_guard lock(_mutex);
    if (_device) {
        return _device->snapshot();
    }
    return std::make_shared<const protocol::DeviceState>();
}

void DeviceService::startAutoConnect(std::string address) {
    std::lock_guard lock(_mutex);
    _target = std::move(address); _automatic = true; _retrySeconds = 1; _released = false;
    _nextAttempt = _now(); _connectionState = "searching";
}
std::string DeviceService::connectionState() const {
    std::lock_guard lock(_mutex);
    return _connectionState == "connected" && !isConnected() ? "retrying" : _connectionState;
}
std::string DeviceService::selectedAddress() const { std::lock_guard lock(_mutex); return _selected; }
std::string DeviceService::lastError() const { std::lock_guard lock(_mutex); return _lastError; }
void DeviceService::wake() {
    std::lock_guard lock(_mutex);
    _lastActivity = _now();
    if (!_released) return;
    try {
        _connect(transport::DeviceAddress(_selected), _selectedName);
    } catch (const std::exception& ex) {
        // Typically another client (e.g. Sound Connect on a phone) holds the channel.
        if (_device) _device->release();
        _released = true; _connectionState = "idle";
        _lastError = std::string("Headset control channel is busy (another app may be using it): ") + ex.what();
        throw SonyException(SonyErrorCode::Disconnected, _lastError);
    }
    Logger::info(LogCategory::Device, "Control channel reopened after idle release");
}

void DeviceService::tick() {
    std::lock_guard lock(_mutex);
    if (_released) {
        // Channel intentionally parked; wake() reopens it. A short periodic peek
        // keeps battery and modes fresh without holding the link.
        if (_peekInterval.count() > 0 && _now() >= _nextPeek) {
            _nextPeek = _now() + _peekInterval;
            try {
                _connect(transport::DeviceAddress(_selected), _selectedName);
                _device->release();
            } catch (const std::exception& ex) {
                Logger::debug(LogCategory::Device, std::string("Idle peek skipped: ") + ex.what());
                if (_device) _device->release();
            }
            _released = true; _wasConnected = false; _connectionState = "idle";
        }
        return;
    }
    if (isConnected()) {
        if (_idleRelease.count() > 0 && _device && _device->capabilities().singleControlClient &&
            _now() - _lastActivity >= _idleRelease) {
            _device->release();
            _released = true; _wasConnected = false; _connectionState = "idle";
            _nextPeek = _now() + _peekInterval;
            Logger::info(LogCategory::Device, "Control channel released after idle (single-client headset)");
            return;
        }
        if (_now() >= _nextSettings) {
            // One inquiry per tick keeps maintenance from monopolizing requests.
            _device->refreshSettingsStep();
            _nextSettings = _now() + std::chrono::milliseconds(500);
        }
        if (_now() >= _nextBattery) {
            _device->refreshBattery(); _nextBattery = _now() + std::chrono::seconds(30);
        }
        return;
    }
    if (!_automatic) return;
    if (_wasConnected) {
        _wasConnected = false;
        _device->disconnect();
        _lastError = "Bluetooth connection lost; reconnecting";
        _nextAttempt = _now(); _connectionState = "retrying";
        Logger::warn(LogCategory::Device, _lastError);
    }
    if (_now() < _nextAttempt) return;
    try {
        auto candidates = discoverDevices();
        if (!_target.empty()) {
            auto found = std::find_if(candidates.begin(), candidates.end(), [this](const auto& d) { return d.address == _target; });
            DiscoveredDevice selected{.address = _target};
            if (found != candidates.end()) selected = *found;
            candidates = {selected};
        }
        _connectionState = "searching";
        for (const auto& candidate : candidates) {
            try { _connect(transport::DeviceAddress(candidate.address), candidate.name); return; }
            catch (const std::exception& ex) { _lastError = ex.what(); if (_device) _device->disconnect(); }
        }
        if (candidates.empty()) _lastError = "No paired Sony device found";
    } catch (const std::exception& ex) { _lastError = ex.what(); }
    _connectionState = "retrying";
    Logger::warn(LogCategory::Device, _lastError + "; retry in " + std::to_string(_retrySeconds) + "s");
    _nextAttempt = _now() + std::chrono::seconds(_retrySeconds);
    _retrySeconds = std::min(30u, _retrySeconds * 2);
}
} // namespace sony::core

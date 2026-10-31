#pragma once
#include <deque>
#include <vector>
#include <stdexcept>
#include <cstdint>
class TwoWire {
public:
    std::deque<std::vector<uint8_t>> reads;
    std::vector<uint8_t> current, tx;
    std::vector<size_t> requests;
    size_t cursor = 0;
    uint8_t end_status = 0;
    TwoWire *getHandle() { return this; }
    void begin() {}
    void setClock(unsigned) {}
    void beginTransmission(uint8_t address) { if (address != 0x24) throw std::runtime_error("address"); tx.clear(); }
    size_t write(uint8_t b) { tx.push_back(b); return 1; }
    size_t write(const uint8_t *p, size_t n) { tx.insert(tx.end(), p, p+n); return n; }
    uint8_t endTransmission() { return end_status; }
    size_t requestFrom(uint8_t address, size_t n) {
        if (address != 0x24) throw std::runtime_error("address");
        requests.push_back(n); cursor = 0;
        if (reads.empty()) { current.clear(); return 0; }
        current = reads.front(); reads.pop_front();
        if (current.size() > n) throw std::runtime_error("test fixture exceeds request");
        return current.size();
    }
    int read() {
        if (cursor >= current.size()) throw std::runtime_error("read past returned bytes");
        return current[cursor++];
    }
};
enum { HAL_OK = 0, HAL_ERROR = 1 };
inline int HAL_I2C_Master_Receive(TwoWire *wire, uint16_t address, uint8_t *out, uint16_t count, unsigned) {
    const auto received = wire->requestFrom(address >> 1, count);
    for(size_t i=0;i<received;++i) out[i]=wire->read();
    return received==count ? HAL_OK : HAL_ERROR;
}
inline int HAL_I2C_Master_Transmit(TwoWire *wire, uint16_t address, uint8_t *data, uint16_t count, unsigned) {
    wire->beginTransmission(address >> 1);
    const auto written=wire->write(data,count);
    return wire->endTransmission()==0 && written==count ? HAL_OK : HAL_ERROR;
}

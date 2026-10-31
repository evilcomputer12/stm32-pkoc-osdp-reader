#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>
#include "Arduino.h"
#include "PN532_HSU.h"
#include "PN532.h"
#include "apdu_bridge.h"
static unsigned serviced;
extern "C" void reader_service_wait() { ++serviced; }
struct FakeUart : PN532Uart {
    std::deque<int> bytes;
    std::vector<uint8_t> reply;
    std::vector<uint8_t> sent;
    uint32_t faults = 0;
    unsigned response_delay = 0;
    bool short_ack = false, tx_fail = false, inject_fault = false;
    bool busy = false;
    bool low_power = false;
    void begin() override {}
    uint32_t errors() const override { return faults; }
    int read() override {
        if (inject_fault) { ++faults; inject_fault = false; }
        if (bytes.empty()) return -1;
        int b=bytes.front(); bytes.pop_front(); return b;
    }
    bool write(const uint8_t *p, size_t n) override {
        sent.assign(p,p+n);
        if (tx_fail) return false;
        if (n==6) { bytes.clear(); busy=false; return true; } // Host abort/resync ACK.
        if (n<8 || p[0]==0x55) return true;
        if (busy) return true; // Outstanding RF command from before MCU reset.
        const bool startup_sam = low_power && p[6]==0x14;
        if (low_power && !startup_sam) return true; // Firmware query cannot leave LowVbat.
        if (startup_sam) low_power=false;
        if (startup_sam) for (unsigned i=0;i<150;++i) bytes.push_back(-1);
        for (int b : {0,0,255}) bytes.push_back(b);
        if (short_ack) return true;
        for (int b : {0,255,0}) bytes.push_back(b);
        for (unsigned i=0;i<response_delay;++i) bytes.push_back(-1);
        if (startup_sam) {
            for (int b : {0,0,255,2,254,0xd5,0x15,0x16,0}) bytes.push_back(b);
        } else {
            for (auto b : reply) bytes.push_back(b);
        }
        return true;
    }
    void response(uint8_t cmd, const std::vector<uint8_t> &payload) {
        const uint8_t len=payload.size()+2;
        reply={0,0,255,len,uint8_t(-len),0xd5,uint8_t(cmd+1)};
        uint8_t sum=0xd5+cmd+1;
        for(auto b:payload) { reply.push_back(b); sum+=b; }
        reply.push_back(uint8_t(-sum)); reply.push_back(0);
    }
};
int main() {
    FakeUart uart; PN532_HSU transport(uart); PN532 pn(transport);
    uart.busy=true; uart.low_power=true;
    uart.response(0x02,{0x32,1,6,7});
    pn.begin();
    assert(pn.getFirmwareVersion()==0x32010607 && !uart.busy && !uart.low_power);
    uint8_t out[258]; size_t written=0;
    uart.response(0x4a,{1,1,0,4,0x20,4,1,2,3,4});
    uint8_t uid[7], uidlen=0;
    assert(pn.readPassiveTargetID(0,uid,&uidlen,80,true) && uidlen==4);
    const uint8_t cmd[]={0,0xa4,4,0};
    std::vector<uint8_t> payload(136,0x42); payload[0]=0; payload[134]=0x90; payload[135]=0;
    uart.response(0x40,payload); memset(out,0xa5,sizeof(out));
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)==ASYMCRED_OK);
    assert(written==135 && out[133]==0x90 && out[134]==0 && out[135]==0xa5 && out[256]==0xa5);
    uint8_t txsum=0; for(size_t i=5;i<uart.sent.size()-1;++i) txsum+=uart.sent[i];
    assert(txsum==0 && uart.sent[0]==0 && uart.sent[2]==255);
    uart.response(0x40,payload); uart.reply[uart.reply.size()-2]^=1;
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)!=ASYMCRED_OK && written==0);
    uart.response(0x40,payload); uart.reply[6]^=1;
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)!=ASYMCRED_OK);
    uart.response(0x40,std::vector<uint8_t>(161,0));
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)!=ASYMCRED_OK);
    uart.response(0x40,payload); uart.reply.resize(12);
    fake_time=0;
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)!=ASYMCRED_OK && fake_time<=1005);
    assert(serviced>0);
    uart.short_ack=true; fake_time=0;
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)!=ASYMCRED_OK && fake_time<=15);
    uart.short_ack=false;
    uart.response(0x40,{}); uart.reply[3]=1; uart.reply[4]=255;
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)!=ASYMCRED_OK);
    uart.response(0x40,{}); uart.reply[3]=255; uart.reply[4]=1;
    assert(reader_apdu_exchange(pn,cmd,sizeof(cmd),out,256,&written)!=ASYMCRED_OK);
    uart.response(0x14,{}); assert(pn.SAMConfig());
    uart.response(0,{0}); assert(pn.checkTargetPresence()==0);
    uart.response(0,{1}); assert(pn.checkTargetPresence()==1);
    uart.response(0,{2}); assert(pn.checkTargetPresence()==2);
    uart.response_delay=250; fake_time=0;
    uart.response(0,{1}); assert(pn.checkTargetPresence()==1 && fake_time==250);
    uart.response_delay=0;
    uart.tx_fail=true; assert(pn.checkTargetPresence()<0); uart.tx_fail=false;
    uart.response(0,{0}); assert(pn.checkTargetPresence()==0); // Recovers after failed TX.
    const uint8_t request=0;
    uart.response(0,{0}); assert(transport.writeCommand(&request,1)==0);
    ++uart.faults; assert(transport.readResponse(out,160,10)<0);
    puts("PASS: HSU large APDU, checksums, bounds, short ACK/frame, wrong command, extended/invalid lengths, total timeout, service hook, UART errors, resync and presence status");
}

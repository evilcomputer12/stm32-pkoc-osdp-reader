#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>
#include "adafruit_reader.h"
#include "apdu_bridge.h"

struct FakeReaderSerial {
    uint32_t consumed=0, faults=0, waits=0;
    std::deque<int> bytes;
    int read() { if(bytes.empty()) return -1; int b=bytes.front(); bytes.pop_front(); if(b>=0) ++consumed; return b; }
    uint32_t errors() const { return faults; }
    void idle() { ++waits; ++fake_time; }
    size_t write(const uint8_t *,size_t n) { bytes.clear(); return n; }
};
struct FakeAdafruit {
    FakeReaderSerial &serial;
    std::vector<uint8_t> response, last_command;
    unsigned ack_count=6, delay_bytes=0, starts=0;
    bool ack_ok=true, inject_fault=false;
    bool begin() { ++starts; return true; }
    bool sendCommandCheckAck(uint8_t *cmd,uint8_t n,uint16_t) {
        last_command.assign(cmd,cmd+n); serial.consumed+=ack_count;
        if(inject_fault) ++serial.faults;
        for(unsigned i=0;i<delay_bytes;++i) serial.bytes.push_back(-1);
        for(auto b:response) serial.bytes.push_back(b);
        return ack_ok;
    }
    void reply(uint8_t cmd,const std::vector<uint8_t> &data) {
        uint8_t n=data.size()+2, sum=0xd5+uint8_t(cmd+1);
        response={0,0,255,n,uint8_t(-n),0xd5,uint8_t(cmd+1)};
        for(auto b:data) { response.push_back(b); sum+=b; }
        response.push_back(uint8_t(-sum)); response.push_back(0);
    }
};
int main() {
    FakeReaderSerial serial; FakeAdafruit vendor{serial};
    AdafruitReader<FakeAdafruit,FakeReaderSerial> pn(vendor,serial);
    pn.begin(); assert(vendor.starts==1);
    vendor.reply(2,{0x32,1,6,7}); assert(pn.getFirmwareVersion()==0x32010607);
    vendor.reply(0x14,{}); assert(pn.SAMConfig());
    vendor.reply(0x32,{}); assert(pn.setPassiveActivationRetries(0) && pn.setRFField(0,1));
    vendor.reply(0x4a,{1,1,0,4,0x20,4,1,2,3,4});
    uint8_t uid[7], uidlen=0;
    assert(pn.readPassiveTargetID(0,uid,&uidlen,80,true) && uidlen==4);
    uint8_t out[258]; size_t written=0;
    const uint8_t request[]={0,0xa4,4,0};
    std::vector<uint8_t> payload(136,0x42); payload[0]=0; payload[134]=0x90; payload[135]=0;
    vendor.reply(0x40,payload); memset(out,0xa5,sizeof(out));
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)==ASYMCRED_OK);
    assert(written==135 && out[133]==0x90 && out[134]==0 && out[135]==0xa5);
    assert(vendor.last_command[0]==0x40 && vendor.last_command[1]==1);
    vendor.reply(0x40,payload); vendor.response[vendor.response.size()-2]^=1;
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)!=ASYMCRED_OK && written==0);
    vendor.reply(0x40,payload); vendor.response[6]^=1;
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)!=ASYMCRED_OK);
    vendor.reply(0x40,std::vector<uint8_t>(162,0));
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)!=ASYMCRED_OK);
    vendor.reply(0x40,payload); vendor.response.resize(10); fake_time=0;
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)!=ASYMCRED_OK && fake_time<=1005);
    vendor.reply(0x40,payload); vendor.ack_count=5; // Upstream falsely accepts a short ACK.
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)!=ASYMCRED_OK);
    vendor.ack_count=6; vendor.inject_fault=true;
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)!=ASYMCRED_OK);
    vendor.inject_fault=false;
    payload[0]=0x40; vendor.reply(0x40,payload); // Unsupported MI/chaining.
    assert(reader_apdu_exchange(pn,request,sizeof(request),out,256,&written)!=ASYMCRED_OK);
    vendor.reply(0x4a,{0}); assert(!pn.readPassiveTargetID(0,uid,&uidlen,80,true) && pn.lastTargetStatus==0);
    vendor.reply(0x4a,{1,1,0,4,0x20,8,1,2,3,4,5,6,7,8});
    assert(!pn.readPassiveTargetID(0,uid,&uidlen,80,true) && uidlen==0);
    vendor.reply(0,{0}); assert(pn.checkTargetPresence()==0);
    vendor.reply(0,{1}); vendor.delay_bytes=250; fake_time=0;
    assert(pn.checkTargetPresence()==1 && fake_time==250);
    vendor.delay_bytes=0; vendor.reply(0,{2}); assert(pn.checkTargetPresence()==2);
    vendor.reply(0,{}); assert(pn.checkTargetPresence()<0);
    vendor.reply(0,{0}); vendor.response[3]=255; vendor.response[4]=1;
    assert(pn.checkTargetPresence()<0);
    assert(serial.waits>0);
    puts("PASS: Adafruit adapter large APDU, checksum/command/length checks, short-ACK guard, timeout, UART error, resync, target bounds, presence and no truncated credentials");
}

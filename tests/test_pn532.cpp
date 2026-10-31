#include <cassert>
#include <algorithm>
#include <cstdio>
#include "Arduino.h"
#include "PN532_I2C.h"
#include "PN532.h"
#include "apdu_bridge.h"
extern "C" void reader_service_wait() {}

static void response(TwoWire &wire, uint8_t command, const std::vector<uint8_t> &payload, bool corrupt=false) {
    const uint8_t length = payload.size()+2;
#ifndef READER_SOFT_I2C
    wire.reads.push_back({1}); // Status-only poll, STOP, then ACK read.
#endif
    wire.reads.push_back({1,0,0,255,0,255,0}); // ACK
#ifndef READER_SOFT_I2C
    wire.reads.push_back({1});
    wire.reads.push_back({1,0,0,255,length,uint8_t(-length)});
#endif
    std::vector<uint8_t> frame={1,0,0,255,length,uint8_t(-length),0xd5,uint8_t(command+1)};
    uint8_t sum=0xd5+command+1;
    for(auto value:payload) { frame.push_back(value); sum+=value; }
    frame.push_back(uint8_t(-sum) ^ (corrupt ? 1 : 0)); frame.push_back(0);
#ifndef READER_SOFT_I2C
    wire.reads.push_back({1});
#endif
    wire.reads.push_back(frame);
}
static void select(PN532 &pn, TwoWire &wire) {
    response(wire,0x4a,{1,1,0,4,0x20,4,1,2,3,4});
    uint8_t uid[7], count;
    assert(pn.readPassiveTargetID(0,uid,&count,80,true) && count==4);
}
int main() {
    TwoWire wire; PN532_I2C transport(wire); PN532 pn(transport);
#ifdef READER_SOFT_I2C
    extern TwoWire *fake_soft_bus;
    fake_soft_bus = &wire;
#endif
    wire.reads.push_back({0}); // Busy: only status may be read before STOP.
    wire.reads.push_back({0});
    select(pn,wire);
#ifndef READER_SOFT_I2C
    assert(wire.requests.size() >= 4 && wire.requests[0] == 1 &&
           wire.requests[1] == 1 && wire.requests[2] == 1 && wire.requests[3] == 7);
#endif
    uint8_t command[]={0,0xa4,4,0};
    uint8_t out[258]; memset(out,0xa5,sizeof(out)); size_t length=0;
    std::vector<uint8_t> payload(136); // PN status + 135-byte R-APDU
    for(size_t i=1;i<134;++i) payload[i]=i;
    payload[134]=0x90; payload[135]=0;
    response(wire,0x40,payload);
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)==ASYMCRED_OK);
    assert(length==135 && out[133]==0x90 && out[134]==0 && out[135]==0xa5 && out[256]==0xa5);
#ifndef READER_SOFT_I2C
    assert(std::find(wire.requests.begin(),wire.requests.end(),146)!=wire.requests.end());
#endif

    response(wire,0x40,payload);
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,134,&length)==ASYMCRED_ERR_BUFFER_TOO_SMALL && length==0);
    response(wire,0x40,payload,true);
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK && length==0);
    payload[0]=0x40; response(wire,0x40,payload);
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK && pn.lastExchangeStatus==0x40);
    response(wire,0x40,{});
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK);
    response(wire,0x40,{0,0x90});
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK);
    response(wire,0x40,std::vector<uint8_t>(161,0));
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK);
    wire.reads.clear();
#ifndef READER_SOFT_I2C
    wire.reads.push_back({1});
#endif
    wire.reads.push_back({1,0,0}); // short ACK
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK);
    wire.end_status=2;
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK);
    wire.end_status=0; fake_time=0; wire.requests.clear();
    wire.reads.push_back({0}); wire.reads.push_back({0});
    assert(reader_apdu_exchange(pn,command,sizeof(command),out,256,&length)!=ASYMCRED_OK && fake_time<=11);
#ifndef READER_SOFT_I2C
    assert(std::all_of(wire.requests.begin(),wire.requests.end(),[](size_t count){return count==1;}));
#endif
    assert(reader_apdu_exchange(pn,command,256,out,256,&length)==ASYMCRED_ERR_INVALID_ARG);
    response(wire,0x4a,{0}); uint8_t uid[7], count;
    assert(!pn.readPassiveTargetID(0,uid,&count,80,true) && pn.lastTargetStatus==0);
    response(wire,0x4a,{1});
    assert(!pn.readPassiveTargetID(0,uid,&count,80,true) && pn.lastTargetStatus<0);
    response(wire,0x00,{0}); assert(pn.checkTargetPresence()==0);
    response(wire,0x00,{1}); assert(pn.checkTargetPresence()==1);
    response(wire,0x00,{2}); assert(pn.checkTargetPresence()==2); // RF CRC is not removal.
    response(wire,0x00,{}); assert(pn.checkTargetPresence()<0);
    response(wire,0x00,{0,0}); assert(pn.checkTargetPresence()<0);
    response(wire,0x00,{1},true); assert(pn.checkTargetPresence()<0);
    puts("PASS: PN532 status-only busy polling, 146-byte frame, 256-byte caller capacity, SW preservation, bounds, checksums, short transfers, NACK, timeout, chaining and absence/error distinction");
}

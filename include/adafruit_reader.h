#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Adafruit owns startup, command framing and ACK handling. Checked, variable
// length response handling here avoids its fixed 64-byte APDU receive buffer.
// Driver is the unmodified Adafruit_PN532; Serial services OSDP while waiting.
template<class Driver, class Serial> class AdafruitReader {
    Driver &driver;
    Serial &serial;
    uint8_t target=0;
    bool resync=false;
    uint32_t io_errors=0;
    int fail(int error) { last_io=error; resync=true; return error; }
    bool receive(uint8_t *out, size_t length, uint32_t start, uint16_t timeout) {
        size_t used=0;
        while (used < length) {
            if (serial.errors()!=io_errors || uint32_t(millis()-start)>=timeout) return false;
            int value=serial.read();
            if (value>=0) out[used++]=uint8_t(value);
            else serial.idle();
        }
        return serial.errors()==io_errors;
    }
    int exchange(uint8_t *command, uint8_t length, uint8_t *out, size_t capacity, uint16_t timeout=1000) {
        stage=1; last_io=0; ack_bytes=uart_errors=0; acknowledged=false; memset(last_frame,0,sizeof(last_frame));
        if (!command || !length || length>253 || !out || !timeout) return fail(-3);
        if (resync) {
            const uint8_t ack[]={0,0,255,0,255,0};
            if (serial.write(ack,sizeof(ack))!=sizeof(ack)) return fail(-2);
            for (unsigned i=0;i<5;++i) serial.idle();
            resync=false;
        }
        // A bounded drain cannot hang on a noisy/continuously transmitting line.
        for (unsigned i=0;i<512;++i) if (serial.read()<0) break;
        io_errors=serial.errors();
        const uint32_t start=millis(), consumed=serial.consumed;
        stage=2;
        const bool acked=driver.sendCommandCheckAck(command,length,timeout);
        acknowledged=acked; ack_bytes=serial.consumed-consumed;
        uart_errors=serial.errors()-io_errors;
        // The upstream readack uses a fixed 6-byte buffer and does not check
        // Stream::readBytes' return value. Require all six bytes to be read.
        if (!acked || serial.consumed-consumed!=6 || serial.errors()!=io_errors)
            return fail(-2);
        stage=3;
        uint8_t prefix[7]={};
        if (!receive(prefix,sizeof(prefix),start,timeout)) return fail(-2);
        memcpy(last_frame,prefix,sizeof(prefix));
        const unsigned size=prefix[3];
        if (prefix[0] || prefix[1] || prefix[2]!=255 || size<2 || size==255 ||
            uint8_t(size+prefix[4]) || prefix[5]!=0xd5 || prefix[6]!=uint8_t(command[0]+1))
            return fail(-3);
        const size_t payload=size-2;
        if (payload>capacity) return fail(-4);
        stage=4;
        if (!receive(out,payload,start,timeout)) return fail(-2);
        uint8_t suffix[2]={};
        if (!receive(suffix,2,start,timeout)) return fail(-2);
        uint8_t sum=0xd5+uint8_t(command[0]+1);
        for(size_t i=0;i<payload;++i) sum+=out[i];
        if(uint8_t(sum+suffix[0]) || suffix[1]) return fail(-3);
        stage=5;
        return int(payload);
    }
public:
    unsigned stage=0;
    uint32_t ack_bytes=0, uart_errors=0;
    bool acknowledged=false;
    int last_io=0, lastTargetStatus=-1, lastExchangeStatus=-1;
    uint8_t last_frame[12]={};
    AdafruitReader(Driver &implementation, Serial &port) : driver(implementation), serial(port) {}
    void begin() { target=0; resync=false; driver.begin(); }
    uint32_t getFirmwareVersion() {
        uint8_t command[]={2}, response[4]={};
        if(exchange(command,sizeof(command),response,sizeof(response))!=4) return 0;
        return uint32_t(response[0])<<24 | uint32_t(response[1])<<16 | uint32_t(response[2])<<8 | response[3];
    }
    bool SAMConfig() {
        uint8_t command[]={0x14,1,0x14,1}, response[1];
        return exchange(command,sizeof(command),response,sizeof(response))==0;
    }
    bool setPassiveActivationRetries(uint8_t retries) {
        uint8_t command[]={0x32,5,0xff,1,retries}, response[1];
        return exchange(command,sizeof(command),response,sizeof(response))==0;
    }
    bool setRFField(uint8_t autoRFCA,uint8_t on) {
        uint8_t command[]={0x32,1,uint8_t(autoRFCA|on)}, response[1];
        return exchange(command,sizeof(command),response,sizeof(response))==0;
    }
    int inRelease(uint8_t which) {
        uint8_t command[]={0x52,which}, response[1];
        return exchange(command,sizeof(command),response,sizeof(response));
    }
    int checkTargetPresence() {
        uint8_t command[]={0,6}, response[1];
        const int result=exchange(command,sizeof(command),response,sizeof(response));
        return result==1 ? response[0] : (result<0 ? result : -3);
    }
    bool readPassiveTargetID(uint8_t baud,uint8_t *uid,uint8_t *uidLength,uint16_t timeout,bool iso4) {
        if(!uid || !uidLength) return false;
        *uidLength=0; lastTargetStatus=-1;
        uint8_t command[]={0x4a,1,baud}, response[64]={};
        const int count=exchange(command,sizeof(command),response,sizeof(response),timeout);
        if(count<0) { lastTargetStatus=count; return false; }
        if(count==1 && response[0]==0) { lastTargetStatus=0; return false; }
        if(count<6 || response[0]!=1 || response[1]==0 || response[5]==0 || response[5]>7 || count<6+response[5]) return false;
        if(iso4 && !(response[4]&0x20)) return false;
        target=response[1]; *uidLength=response[5]; memcpy(uid,response+6,*uidLength);
        lastTargetStatus=1; return true;
    }
    bool inDataExchange(const uint8_t *data,uint8_t length,uint8_t *out,uint8_t *outLength) {
        if(!outLength) return false;
        const uint8_t capacity=*outLength; *outLength=0; lastExchangeStatus=-1;
        if(!target || !data || !length || length>251 || !out) return false;
        uint8_t command[253]={0x40,target}, response[161]={};
        memcpy(command+2,data,length);
        const int count=exchange(command,length+2,response,sizeof(response));
        if(count<0) { lastExchangeStatus=count; return false; }
        if(count<1) return false;
        lastExchangeStatus=response[0];
        // MI/chaining is not implemented: never accept an incomplete APDU.
        if(response[0] || count-1>capacity) return false;
        memcpy(out,response+1,count-1); *outLength=count-1;
        return true;
    }
};

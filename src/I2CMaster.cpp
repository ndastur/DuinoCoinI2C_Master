#include "config.h"
#include "utils.h"
#include "I2CMaster.h"

//#define DEBUG_FULL 1

static inline uint8_t crc8_maxim(const uint8_t* data, size_t len, uint8_t crc = 0x00) {
  for (size_t i = 0; i < len; ++i) {
    uint8_t in = data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      uint8_t mix = (crc ^ in) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C; // 0x8C is 0x31 reflected (for right-shift)
      in >>= 1;
    }
  }
  return crc;
}

static inline uint8_t crc8_maxim(const uint8_t data, size_t len, uint8_t crc = 0x00) {
    const uint8_t dataArray[1] = { data };
    return crc8_maxim(dataArray, len, crc);
}

I2CMaster::I2CMaster(int sdaPin, int sclPin, uint32_t freq, bool doBegin)
    : _sdaPin(sdaPin), _sclPin(sclPin), _freq(freq) {
        begin();
    }

void I2CMaster::begin() {
    DEBUGPRINT("[I2C] Starting wire with pins. SDA: ");
    DEBUGPRINT(_sdaPin);
    DEBUGPRINT(" SCL: ");
    DEBUGPRINT(_sclPin);
    DEBUGPRINT(" Freq: ");
    DEBUGPRINT(_freq);
    DEBUGPRINT_LN();

    Wire.begin(_sdaPin, _sclPin, _freq);
    delay(50);
    Wire.setTimeOut(300);
}

void I2CMaster::setTimeout(uint16_t timeout) {
    _timeout = timeout;
    Wire.setTimeOut(timeout);
}

bool I2CMaster::probe(uint8_t address) {
    for (int attempt = 0; attempt < _retries; ++attempt) {
        Wire.beginTransmission(address);
        uint8_t err = Wire.endTransmission();
        if (err == 0) return true;
        delay(_scanDelayMs);
    }
    return false;
}

uint8_t I2CMaster::getFoundSlaveCount() {
    return _slaveCount;
}

I2CMaster::I2C_SLAVE* I2CMaster::getFoundSlave(uint8_t idx) {
    return (idx > _slaveCount) ? 0 : &_slaves[idx];    
}

uint8_t I2CMaster::getFoundSlaveAddress(uint8_t idx) {
    return (idx > _slaveCount) ? 0 : _slaves[idx].address;
}

void I2CMaster::scan(bool getIds) {
    _slaveCount = 0;
    memset(_slaves, 0, sizeof(_slaves));

    bool found = false;
    for (uint8_t addr = 1; addr < 127; ++addr) {
        if (probe(addr)) {
            _slaves[_slaveCount++].address = addr;
            found = true;
        }
    }

    // Do this in it's own loop so slaves get saved
    if(getIds) {
        for(int i=0; i < _slaveCount; i++) {
            uint8_t resp[8];
            if(!_sendCmd(_slaves[i].address, CMD_GET_UNIQUEID))
                continue;
            if(!_getResponse(_slaves[i].address, 8, resp))
                continue;
            DEBUGPRINT("Unique ID: ");
            for(int x=0;x<8;x++) {
                byte b1=resp[x] >> 4;
                byte b2=resp[x] & 0x0f;
                b1+='0'; if (b1>'9') b1 += 7;  // gap between '9' and 'A'
                b2+='0'; if (b2>'9') b2 += 7;
                _slaves[i].slaveUniqueID[x*2] = b1;
                _slaves[i].slaveUniqueID[(x*2)+1] = b2;
            }
            _slaves[i].slaveUniqueID[(8*2)] = 0;
        }
    }

    if (!found) DEBUGPRINT_LN("[I2C] Scan - no devices found.");
}

void I2CMaster::dumpSlaves() {
    SERIALPRINT_LN("I2C device list:");
    for(int i=0; i < _slaveCount; i++) {
        SERIALPRINT("  -> device @ 0x");
        SERIALPRINT_HEX(_slaves[i].address);
        SERIALPRINT(" uniq id: ");
        SERIALPRINT(_slaves[i].slaveUniqueID);
        SERIALPRINT_LN();
    }
}

bool I2CMaster::version(uint8_t address, uint8_t &ver_major, uint8_t &ver_minor) {
    if(!_sendCmd(address, CMD_VERSION)) return false;

    uint32_t start = millis();
    while (millis() - start < _timeout) {
        if (Wire.requestFrom((int)address, 1) == 1) {
            ver_major = (uint8_t)Wire.read();
            ver_minor = (uint8_t)Wire.read();
        }
        delay(5);
    }
    return false;
}

bool I2CMaster::queryUptime(uint8_t address, uint32_t &outMillis) {
    if(!_sendCmd(address, CMD_GET_UPTIME)) return false;

    uint8_t res[4];
    if(_getResponse(address, 4, res)) {
        uint32_t v = 0;
        v |= (uint32_t)res[0];
        v |= (uint32_t)res[1] << 8;
        v |= (uint32_t)res[2] << 16;
        v |= (uint32_t)res[3] << 24;
        outMillis = v;
        return true;
    }
    
    return false;
}

bool I2CMaster::queryUniqueId(uint8_t address, uint8_t id[8]) {
    if(!_sendCmd(address, CMD_GET_UNIQUEID)) return false;

    return _getResponse(address, 8, id);
}

bool I2CMaster::sendDataBegin(uint8_t address) {
    if(!_sendCmd(address, CMD_BEGIN_DATA)) return false;

    uint8_t data[1];
    if ( ! _getResponse(address, 1, data) ) {
        return false;
    }

    DEBUGPRINT("[I2C] sendDataBegin Ret status. Addr: 0x");
    DEBUGPRINT_HEX(address);
    DEBUGPRINT(" Status: 0x");
    DEBUGPRINT_HEX(data[0]);
    DEBUGPRINT_LN();

    return (data[0] == 0xAA);
}

bool I2CMaster::sendData(uint8_t address, const uint8_t *data, const uint8_t len, const uint8_t startSeq) {
    // TODO add timeout
    #if defined(DEBUG_FULL)
        DEBUGPRINT( "[I2C] sendData() to 0x" );
        DEBUGPRINT_HEX(address);
        DEBUGPRINT(" len: ");
        DEBUGPRINT(len);
        DEBUGPRINT_LN();
    #endif

    uint8_t i = 0;
    uint8_t dataBuf[2];
    uint8_t loopRetry = 0;
    uint8_t sendRespRetry = 0;
    while(i < len) {
        dataBuf[0] = i + startSeq;         // sequence
        dataBuf[1] = data[i];   // actual data
        if (!_sendCmd(address, CMD_SEND_DATA, dataBuf, 2)) {
            if(sendRespRetry++ < 3) {
                continue;       // try again
            }
            else {
                return false;
            }
        }
        sendRespRetry = 0;

        uint8_t respBuf[3];
        if(!_getResponse(address, 3, respBuf)) {
            DEBUGPRINT_LN(respBuf[1]);
            if(sendRespRetry++ < 2) {
                continue;       // try again
            }
            else {
                return false;
            }
        }
        sendRespRetry = 0;

        if(respBuf[0] == 0xAA && respBuf[1] == i + startSeq && respBuf[2] == data[i]) {
            i++;    // move on
            continue;
        }
        else {
            DEBUGPRINT("   Error response from send data: 0x");
            DEBUGPRINT_HEX(respBuf[0]);
            DEBUGPRINT(" ");
            DEBUGPRINT_LN(respBuf[0]);
            if(loopRetry++ < 2) {
                continue;
            }
            else {
                return false;
            }
        }
    }   // end while

    return (i == len) ? true : false;
}

/// @brief Send the job data to the slave
bool I2CMaster::sendJobData(uint8_t address, const char *previousHashStr,
    const char *expectedHashStr, uint8_t difficulty) {

    if(!sendDataBegin(address)) {
        SERIALPRINT_LN("[I2C] error from send data begin check.");
        return false;
    }

    DEBUGPRINT("[I2C] sending job to 0x");
    DEBUGPRINT_HEX(address);
    DEBUGPRINT_LN();

    uint8_t resp[4];    // max response size

    // ----------------------------------
    // Make a packet array to send
    // ----------------------------------
    uint8_t job_packet[41+20+1];
    memcpy(job_packet, previousHashStr, 41);    // null ending prev hash string
    hexStringToUint8Array(expectedHashStr, &job_packet[41] ,20);  // convert to byte array directly into the packet
    job_packet[41+20] = difficulty;
    if( !sendData(address, job_packet, 41+20+1) ) return false;

    u_int8_t crc8[1] = { crc8_maxim(job_packet, 41+20+1) };

    delay(2); // Give slave a chance to load data and process a CRC on device
    if( !_sendCmd(address, CMD_END_DATA, crc8, 1) ) return false;
    
    if(!_getResponse(address, 1, resp)) return false;
    if( resp[0] == 0xAA) {
        return true;
    }
    else {
        DEBUGPRINT("[I2C] sendJob CMD_END_DATA error: 0x");
        DEBUGPRINT_HEX(resp[0]);
        DEBUGPRINT_LN();
        return false;
    }
    return true;
}

bool I2CMaster::getJobStatus(uint8_t address) {
    if( !_sendCmd(address, CMD_GET_JOB_STATUS) ) return false;

    uint8_t resp[4];
    if(!_getResponse(address, 1, resp)) return false;
    return ( resp[0] == 0xAA);
}

bool I2CMaster::getJobResult(uint8_t address, uint16_t &foundNonce, uint16_t &timeTakenMs) {
    if( !getJobStatus(address) ) {
        return false;
    }

    if( !_sendCmd(address, CMD_GET_JOB_RESULT) ) return false;

    uint8_t resp[5];
    if(!_getResponse(address, 5, resp)) return false;
    if( resp[0] == 0xAA) {
        foundNonce   = (uint16_t)resp[1];
        foundNonce  |= (uint16_t)resp[2] << 8;
        timeTakenMs  = (uint16_t)resp[3];
        timeTakenMs |= (uint16_t)resp[4] << 8;

        DEBUGPRINT("[I2C] Job Status True: \n   - Nonce: ");
        DEBUGPRINT(foundNonce);
        DEBUGPRINT("\n   - Time: ");
        DEBUGPRINT_LN(timeTakenMs);
        return true;
    }

    return false;
}

/**
 * ************** PRIVATES ***************
 */
bool I2CMaster::_sendCmd(uint8_t address, const uint8_t cmd, const uint8_t data[], uint8_t len, bool sendStop) {
    Wire.beginTransmission((uint16_t)address);
    Wire.write(cmd);
    if(len > 0) Wire.write(data, len);
    
    #if defined DEBUG_FULL
      DEBUGPRINT("[I2C] Sending cmd: 0x");
      DEBUGPRINT_HEX(cmd);
    #endif

    #if defined(DEBUG_FULL)
    if(len > 0) {
        DEBUGPRINT(" Data: ");
        for(int c=0;c<len;c++) {
        DEBUGPRINT_HEX( data[c] );
        }
    }
    DEBUGPRINT_LN(" | END");
    #endif


    // if(len > 0) {
    //     for(uint8_t x = 0; x < len; x++) {
    //         Wire.write(data[x]);
    //     }
    // }
    int8_t ret = Wire.endTransmission(sendStop);
    #if defined(DEBUG_PRINT)
        if(ret!=0) DEBUGPRINT(F("[I2C _sendCmd Error - ]"));
        switch (ret)
        {
        case 1:
            DEBUGPRINT_LN(F("Data too long for Tx buf"));  break;
        case 2:
            DEBUGPRINT_LN(F("NACK on Tx of addr"));  break;
        case 3:
            DEBUGPRINT_LN(F("NACK on Tx of data"));  break;
        case 4:
            DEBUGPRINT_LN(F("Other error"));  break;
        case 5:
            DEBUGPRINT_LN(F("Timeout"));  break;
        default:
            break;
        }
    #endif
    return (ret != 0) ? false : true;
}

bool I2CMaster::_getResponse(uint8_t address, uint8_t respLength, uint8_t data[], bool sendStop) {
    uint32_t start = millis();
    while (millis() - start < _timeout) {
        if (Wire.requestFrom((uint16_t)address, (size_t)respLength, sendStop) == respLength) {
            if (!Wire.available()) {
                break; // unexpected; fall through to retry
            }
            
            #if defined DEBUG_FULL
                DEBUGPRINT("[I2C] Got response data: ");
            #endif
            for (uint8_t c = 0; c < respLength; c++) {
                data[c] = Wire.read();
                #if defined DEBUG_FULL
                    DEBUGPRINT_HEX( data[c] );
                #endif
            }
            #if defined DEBUG_FULL
                DEBUGPRINT_LN(" | END");
            #endif
            
            while(Wire.available()) Wire.read();    // flush
            return true;
        }
        delay(2);
    }
    return false;
}
#include "I2CMaster.h"
#include "config.h"

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

I2CMaster::I2CMaster(int sdaPin, int sclPin, uint32_t freq)
    : _sdaPin(sdaPin), _sclPin(sclPin), _freq(freq) {}

void I2CMaster::begin() {
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

uint8_t I2CMaster::getFoundSlave(uint8_t idx) {
    return (idx > _slaveCount) ? 0 : _slaves[idx];
}

void I2CMaster::scan(bool printToSerial) {
    _slaveCount = 0;
    memset(_slaves, 0, sizeof(_slaves));

    Serial.println("I2C scan:");
    bool found = false;
    for (uint8_t addr = 1; addr < 127; ++addr) {
        if (probe(addr)) {
            _slaves[_slaveCount++] = addr;
            if(printToSerial) {
                char strBuf[80];
                snprintf(strBuf, sizeof(strBuf), "  -> Device at 0x%02X (%d)", addr, addr);
                SERIALPRINT_LN(strBuf);
            }
            found = true;
        }
    }
    if (!found) Serial.println("  No devices found.");
}

bool I2CMaster::ping(uint8_t address, uint8_t expectedResponse) {
    if(!_sendCmd(address, CMD_PING)) return false;

    uint32_t start = millis();
    while (millis() - start < _timeout) {
        if (Wire.requestFrom((int)address, 1) == 1) {
            return (Wire.read() == expectedResponse);
        }
        delay(5);
    }
    return false;
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

bool I2CMaster::queryHeap(uint8_t address, uint32_t &outHeapBytes) {
    if(!_sendCmd(address, CMD_GET_HEAP)) return false;

    uint32_t start = millis();
    while (millis() - start < _timeout) {
        if (Wire.requestFrom((int)address, 4) == 4) {
            uint32_t v = 0;
            v |= (uint32_t)Wire.read();
            v |= (uint32_t)Wire.read() << 8;
            v |= (uint32_t)Wire.read() << 16;
            v |= (uint32_t)Wire.read() << 24;
            outHeapBytes = v;
            return true;
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

bool I2CMaster::queryChipId(uint8_t address, uint8_t sig[3]) {
    if(!_sendCmd(address, CMD_GET_CHIP)) return false;

    uint32_t start = millis();
    while (millis() - start < _timeout) {
        if (Wire.requestFrom((int)address, 3) == 3) {
            sig[0] = Wire.read(); // manufacturer
            sig[1] = Wire.read(); // family
            sig[2] = Wire.read(); // device
            return true;
        }
        delay(5);
    }
    return false;
}

#if defined(TEST_FUNCS)
bool I2CMaster::testSend(uint8_t address, uint8_t bytesToSend) {
    if(bytesToSend > 64) {
        SERIALPRINT_LN("Way too many bytes even for a test ... ");
        return false;
    }

    uint8_t data[bytesToSend];
    for(uint8_t x = 0; x < bytesToSend; x++) {
        data[x] = x;
    }
    _sendCmd(address, CMD_TEST_SEND, data, bytesToSend);

    uint32_t start = millis();
    while (millis() - start < _timeout) {
        if (Wire.requestFrom((int)address, 4) == 4) {
            uint8_t status = Wire.read();
            uint8_t count = Wire.read();
            uint8_t checksum = Wire.read();
            uint8_t crc = Wire.read();
            SERIALPRINT_LN("SEND TEST RTN: " + String(status, HEX)
                + "\n  - Count:  " + String(count)
                + "\n  - ChkSum: " + String(checksum)
                + "\n  - crc8:   " + String(crc)
            );
            return true;
        }
        delay(5);
    }
    return false;
}

bool I2CMaster::testDumpData(uint8_t address) {
    return _sendCmd(address, CMD_TEST_DUMP_DATA);
}

#endif

bool I2CMaster::newJobRequest(uint8_t address) {
    // if(!_sendCmd(address, CMD_NEW_JOB)) return false;
    if(!_sendCmd(address, CMD_BEGIN_DATA)) return false;

    uint8_t data[1];
    if ( ! _getResponse(address, 1, data) ) {
        return false;
    }

    char str[64];
    sprintf(str, "Return status from device addr: 0x%.2X = 0x%.2X",address, data[0]);
    SERIALPRINT_LN( str );

    return (data[0] == 0xAA);
}

bool I2CMaster::sendData(uint8_t address, const uint8_t *data, const uint8_t len, const uint8_t startSeq) {
    // TODO add timeout
    SERIALPRINT_LN( "[I2C] sendData(...)" );

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
            SERIALPRINT_LN(respBuf[1]);
            if(sendRespRetry++ < 3) {
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
            SERIALPRINT("   Error response from send data: ");
            SERIALPRINT_LN(respBuf[0]);
            if(loopRetry++ < 3) {
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
/// The packet frame is: [CMD|SOF][LEN][SEQ][PAYLOAD ...][CRC8] max 16 bytes inc cmd bytes
bool I2CMaster::sendJobData(uint8_t address, const uint8_t *previousHashStr,
    const uint8_t *expectedHash, uint8_t difficulty) {

    uint8_t resp[4];    // max response size

    if( !sendData(address, previousHashStr, 41) ) return false;
    if( !sendData(address, expectedHash, 20, 41) ) return false;
    uint8_t diff[1];
    diff[0] = difficulty;
    if( !sendData(address, diff, 1, 61) ) return false;

    u_int8_t crc8[1] = { crc8_maxim(previousHashStr, 41) };
    crc8[0] = crc8_maxim(expectedHash, 20, crc8[0]);
    crc8[0] = crc8_maxim(difficulty, 1, crc8[0]);

    delay(2); // Give slave a chance to load data and process a CRC on device
    if( !_sendCmd(address, CMD_END_DATA, crc8, 1) ) return false;
    
    if(!_getResponse(address, 1, resp)) return false;
    if( resp[0] == 0xAA) {
        return true;
    }
    else {
        SERIALPRINT("[I2C] sendJob CMD_END_DATA error: 0x");
        SERIALPRINT_HEX(resp[0]);
        SERIALPRINT_LN();
        return false;
    }
    return true;
}

bool I2CMaster::getSlaveIsIdle(uint8_t address) {
    if(!_sendCmd(address, CMD_GET_IS_IDLE)) return false;

    uint8_t resp[1];
    if(!_getResponse(address, 1, resp)) return false;
    return ( resp[0] == 0xAA ) ? true : false;
}

bool I2CMaster::getJobStatus(uint8_t address, uint16_t &foundNonce, uint8_t &timeTakenMs) {
    if( !_sendCmd(address, CMD_GET_JOB_STATUS) ) return false;

    uint8_t resp[4];
    if(!_getResponse(address, 4, resp)) return false;
    if( resp[0] == 0xAA) {
        foundNonce = (uint16_t)resp[1];
        foundNonce |= (uint16_t)resp[2] << 8;
        timeTakenMs = resp[3];
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
    
    char hex[4];
    SERIALPRINT("[I2C] Sending cmd: 0x");
    sprintf(hex, "%.2X ", cmd);
    SERIALPRINT(hex);
    #if defined(SERIAL_PRINT)
    if(len > 0) {
        SERIALPRINT(" Data: ");
        for(int c=0;c<len;c++) {
        sprintf(hex, "%.2X ", data[c]);
        SERIALPRINT( hex );
        }
    }
    #endif
    SERIALPRINT_LN(" | END");


    // if(len > 0) {
    //     for(uint8_t x = 0; x < len; x++) {
    //         Wire.write(data[x]);
    //     }
    // }
    int8_t ret = Wire.endTransmission(sendStop);
    #if defined(SERIAL_PRINT)
        if(ret!=0) SERIALPRINT(F("[I2C _sendCmd Error - ]"));
        switch (ret)
        {
        case 1:
            SERIALPRINT_LN(F("Data too long for Tx buf"));  break;
        case 2:
            SERIALPRINT_LN(F("NACK on Tx of addr"));  break;
        case 3:
            SERIALPRINT_LN(F("NACK on Tx of data"));  break;
        case 4:
            SERIALPRINT_LN(F("Other error"));  break;
        case 5:
            SERIALPRINT_LN(F("Timeout"));  break;
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
            SERIALPRINT("[I2C] Got response data: ");
            for (uint8_t c = 0; c < respLength; c++) {
                data[c] = Wire.read();
                #if defined(SERIAL_PRINT)
                    char hex[4];
                    sprintf(hex, "%.2X ", data[c]);
                    SERIALPRINT( hex );
                #endif
            }            
            SERIALPRINT_LN(" | END");
            return true;
        }
        delay(5);
    }
    return false;
}
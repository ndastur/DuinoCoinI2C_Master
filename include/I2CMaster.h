#pragma once
#include <Arduino.h>
#include <Wire.h>

#define I2C_SDA     21
#define I2C_SCL     22
#define I2C_FREQ    100000UL
#define MAX_I2C_WORKERS 30

class I2CMaster {
public:
    struct I2C_SLAVE {
        uint8_t address;
        char slaveUniqueID [(8*2)+1];
        bool isMining;
    };

    I2CMaster(int sdaPin = I2C_SDA, int sclPin = I2C_SCL, uint32_t freq = I2C_FREQ, bool doBegin = true);

    // Must be called from setup and only once
    void begin();

    // Set the timeout in ms
    void setTimeout(uint16_t timeout);

    /// Scan bus, get the unique IDs of the devices
    void scan(bool getIds = false);

    /// Dump slave info to serial
    void dumpSlaves();

    /// Check if a device ACKs its address
    bool probe(uint8_t address);

    /// Get the number of slave devices found
    uint8_t getFoundSlaveCount();

    /// get address of slave at index idx
    I2C_SLAVE *getFoundSlave(uint8_t idx);

    /// get address of slave at index idx
    uint8_t getFoundSlaveAddress(uint8_t idx);

    /// Get the version on the slave
    bool version(uint8_t address, uint8_t &ver_major, uint8_t &ver_minor);

    // Request uptime (ms) as 32-bit
    bool queryUptime(uint8_t address, uint32_t &outMillis);

    // Request chip signature (8 bytes)
    bool queryUniqueId(uint8_t address, uint8_t id[8]);

#if defined(TEST_FUNCS)
    /// Test sending different bytes lengths
    bool testSend(uint8_t address, uint8_t bytesToSend = 8);
    bool testDumpData(uint8_t address);
#endif

    /// Check if the slave is in a state to receive a new job
    bool sendDataBegin(uint8_t address);

    /// Send job data
    bool sendJobData(uint8_t address, const char *previousHashStr, const char *expectedHash, uint8_t difficulty);

    /// Send data
    bool sendData(uint8_t address, const uint8_t *data, const uint8_t len, const uint8_t startSeq = 0);

    bool getSlaveIsIdle(uint8_t address);

    // Get the status of the job and if found the nonce and timings
    bool getJobStatus(uint8_t address, uint16_t &foundNonce, uint8_t &timeTakenMs);

private:
    int _sdaPin;
    int _sclPin;
    uint32_t _freq;
    uint16_t _timeout = 300;

    static constexpr uint8_t _retries = 2;
    static constexpr uint16_t _scanDelayMs = 5;

    uint8_t _slaveCount = 0;
    I2C_SLAVE _slaves[MAX_I2C_WORKERS];

    // uint8_t _slaves[MAX_I2C_WORKERS];
    // char _slaveUniqueIDs [MAX_I2C_WORKERS][(8*2)+1];

    // Protocol command IDs
    static constexpr uint8_t CMD_VERSION    = 0x02;
    static constexpr uint8_t CMD_GET_UPTIME = 0x06;

    static constexpr uint8_t CMD_BEGIN_DATA   = 0x20;
    static constexpr uint8_t CMD_SEND_DATA    = 0x22;
    static constexpr uint8_t CMD_END_DATA     = 0x24;

    static constexpr uint8_t CMD_GET_IS_IDLE    = 0x30;
    static constexpr uint8_t CMD_GET_JOB_STATUS = 0x32;
    static constexpr uint8_t CMD_GET_JOB_DATA   = 0x35;
    static constexpr uint8_t CMD_GET_UNIQUEID   = 0x40;

    static constexpr uint8_t CMD_TEST_SEND  = 0x90;
    static constexpr uint8_t CMD_TEST_DUMP_DATA  = 0x92;

    bool _sendCmd(uint8_t address, const uint8_t cmd, const uint8_t data[] = nullptr, uint8_t len = 0, bool sendStop = true);
    // Get response from slave
    bool _getResponse(uint8_t address, uint8_t respLength, uint8_t data[], bool sendStop = true);
};
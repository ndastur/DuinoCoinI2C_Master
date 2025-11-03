#ifndef MINER_CLIENT_H
#define MINER_CLIENT_H

#include "pool.h"
#include "I2CMaster.h"
#include "runevery.h"

#include <DSHA1.h>
#include <Arduino.h>
#include <WiFiClient.h>

#define CLIENT_CONNECT_EVERY 30000
#define AVR_WORKER_MINER "AVR I2C v4.3"

// --- Events ---
enum MinerEvent : uint8_t {
  ME_SOLVED,           // payload: nonce / hashrate_khs
  ME_SOLVE_FAILED,
  ME_ERROR,            // payload: text = error message
  ME_LOG               // optional general logs
};

struct MinerEventData {
  // Common text buffer
  const char* text = nullptr;

  // Job fields (when ME_JOB)
  const char* seed40 = nullptr;    // 40-char ASCII
  const char* target40 = nullptr;  // 40-char ASCII
  uint16_t    diff = 0;

  // Solve/submit fields (when ME_SOLVED / result)
  uint32_t nonce = 0;
  float    hashrate_khs = 0.0f;
};

// C-style callback to avoid pulling in <functional>
typedef void (*MinerEventCallback)(MinerEvent ev, const MinerEventData& data);

class MinerClient {
  public:
    MinerClient(const String username, bool isMaster);
    
    void init();
    Pool* getAttachedPool(int idx = 0);

    // Register a generic event callback
    void onEvent(MinerEventCallback cb);
    void reset();
    void setMining(bool flag = true);
    bool setupSlaves();

    void loop();
    bool findNonce(const char *seed40, const char *target40, uint32_t diff, uint32_t &nonce_found, uint32_t &elapsed_time_us);

  private:
    // State Machine
    enum DUINO_STATE : uint8_t {
      DUINO_STATE_NONE,
      DUINO_STATE_IDLE,
      DUINO_STATE_JOB_REQUEST,
      DUINO_STATE_JOB_WAIT,
      DUINO_STATE_MINING,
      DUINO_STATE_MINING_I2C,
      DUINO_STATE_SHARE_SUBMITTED,
    };

    // config / identity
    String _username;

    // The starting diff can be a number but also worker type (AVR | ESP32 | ESP32S (single core))
    bool _isMasterMiner = false;

    RunEvery _reportTimer = RunEvery(30 * 1000);

    I2CMaster* _i2c = nullptr;
    int _numMinerClients;
    struct ClientStruct
    {
      Pool* _pool = nullptr;      
      uint8_t _address;
      uint32_t  _stateStartMS = 0;
      enum DUINO_STATE _state = DUINO_STATE_NONE;
      unsigned long _jobStartTime;
      char seed[41] = {0};
      char target[41] = {0};
      uint32_t diff = 0;
      uint32_t lastNonce = 0;
      uint16_t lastTimeTakenMs = 0;
      float lastHashRate = 0;
      // How often the slave should be pinged to check for a result
      RunEvery slaveMiningStatusTimer = RunEvery(40);
      // Minimum time to re-request job from the pool
      RunEvery slaveJobReqTimer = RunEvery(25);

      // Stats
      uint32_t startTimeMs = 0;
      uint32_t stats_share_count = 0;
      uint32_t stats_good_count = 0;
      uint32_t stats_block_count = 0;
      uint32_t stats_bad_count = 0;

      uint16_t lowestHashWithError = INT16_MAX;
      uint16_t highestHashWithError = 0;
    };
    
    ClientStruct _clients[MAX_I2C_WORKERS];
    bool _isMining = false;
    DSHA1 *_dsha1;

    // hashrate calc
    uint32_t _masterLastHashedPerSec = 0;
    float _masterLastHashrateKhs = 0.0f;

    // Error
    String _last_err;

    // callback
    MinerEventCallback _cb = nullptr;

    void _setState(DUINO_STATE state, int idx);
    bool _isStateStuck(int idx);

    static void _poolEventSink(PoolEvent ev, const PoolEventData& d, void *user);
    
    bool _solveAndSubmit(const char *seed40, const char *target40, uint32_t diff);

    bool _max_micros_elapsed(unsigned long current, unsigned long max_elapsed);
    void _handleSystemEvents();

    void _printReport();

    // Event functions
    inline void _emit(MinerEvent ev, const MinerEventData& d) {
      if (_cb) _cb(ev, d);
    }

    // Helper to emit simple text events
    inline void _emit_text(MinerEvent ev, const char* msg) {
      MinerEventData d; d.text = msg; _emit(ev, d);
    }
    // Helper to emit just event
    inline void _emit_nodata(MinerEvent ev) {
      MinerEventData d; _emit(ev, d);
    }
};

#endif /* MINER_CLIENT_H */

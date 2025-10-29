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
#define MAX_SLAVES 10

// --- Events ---
enum MinerEvent : uint8_t {
  ME_SOLVED,           // payload: nonce / hashrate_khs
  ME_SOLVE_FAILED,
  ME_RESULT_GOOD,      // payload: share accepted
  ME_RESULT_BAD,       // payload: share rejected
  ME_RESULT_BLOCK,   // payload: share blocked/rate-limited
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
    bool findNonce(const String& seed40, const String& target40, uint32_t diff, uint32_t &nonce_found, uint32_t &elapsed_time_us);

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

    I2CMaster* _i2c = nullptr;
    int _numMinerClients;
    struct ClientStruct
    {
      Pool* _pool = nullptr;
      uint8_t _address;
      uint32_t  _stateStartMS = 0;
      enum DUINO_STATE _state = DUINO_STATE_NONE;
      RunEvery _slaveMiningStatusTimer = RunEvery(200);
      unsigned long _jobStartTime;
      String seed;
      String target;
      uint32_t diff;
    };
    
    ClientStruct _clients[MAX_SLAVES];
    bool _isMining = false;
    DSHA1 *_dsha1;

    // hashrate calc
    uint32_t _last_hashed_per_sec = 0;
    float _last_hashrate_khs = 0.0f;

    // Error
    String _last_err;

    unsigned int _share_count = 0;
    unsigned int _accepted_count = 0;
    unsigned int _block_count = 0;
    unsigned int _last_share_count = 0;

    unsigned long _poolConnectTime = 0;

    // callback
    MinerEventCallback _cb = nullptr;

    void _setState(DUINO_STATE state, int idx);
    bool _isStateStuck(int idx);

    bool _max_micros_elapsed(unsigned long current, unsigned long max_elapsed);
    void _handleSystemEvents();
    void _set_chip_id();
    
    // Protocol steps
    bool _handleMotd();
    bool _solveAndSubmit(const String& seed40, const String& target40, uint32_t diff);
    uint8_t *_hexStringToUint8Array(const String &hexString, uint8_t *uint8Array, const uint32_t arrayLength);

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

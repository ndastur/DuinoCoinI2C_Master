#ifndef MINER_CLIENT_H
#define MINER_CLIENT_H

#include <DSHA1.h>
#include <Arduino.h>
#include <WiFiClient.h>

#define CLIENT_CONNECT_EVERY 30000
#define CLIENT_TIMEOUT_CONNECTION 30000
#define CLIENT_TIMEOUT_REQUEST 100

#define END_TOKEN  '\n'
#define SEP_TOKEN  ','
#define BAD "BAD"
#define GOOD "GOOD"
#define BLOCK "BLOCK"

#define HASHRATE_FORCE false
#define HASHRATE_SPEED 258.0

// --- Events ---
enum MinerEvent : uint8_t {
  ME_CONNECTED,        // payload: text = "host:port"
  ME_DISCONNECTED,     // payload: text = reason
  ME_MOTD,             // payload: text = motd
  ME_JOB_REQUESTED,    //
  ME_JOB_RECEIVED,     // payload: seed40 / target40 / diff
  ME_SOLVED,           // payload: nonce / hashrate_khs
  ME_SOLVE_FAILED,
  ME_RESULT_GOOD,      // payload: share accepted
  ME_RESULT_BAD,       // payload: share rejected
  ME_RESULT_BLOCKED,   // payload: share blocked/rate-limited
  ME_ERROR,            // payload: text = error message
  ME_LOG               // optional general logs
};

struct MinerEventData {
  // Common text buffer (optional)
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
typedef void (*MinerEventCallback)(MinerEvent ev, const MinerEventData& data, void* user);

class MinerClient {
  public:
    MinerClient(const String& username, String minername);
    MinerClient(String host, int port, const String& username, String minername);
    
    void init();
    void setMasterMiner(bool);
    void setPool(const String& host, int port);
    
    // Register a generic event callback
    void onEvent(MinerEventCallback cb, void* user_ctx = nullptr);

    bool connect();
    bool isConnected();
    void reset();
    void start_mining();
    void stop_mining();

    String get_chip_id();

    String get_miner_name();
     // Optional: set rig/session id (else a random hex is used)
    void set_miner_name(const String& name);
   
    void request_motd();

    bool findNonce(const String& seed40, const String& target40, uint32_t diff, uint32_t &nonce_found, uint32_t &elapsed_time_us);

    void loop();

  private:
    // State Machine
    enum Duino_State : uint8_t {
      DUINO_STATE_NONE,
      DUINO_STATE_IDLE,
      DUINO_STATE_VERSION_WAIT,
      DUINO_STATE_MOTD_REQUEST,
      DUINO_STATE_MOTD_WAIT,
      DUINO_STATE_JOB_REQUEST,
      DUINO_STATE_JOB_WAIT,
      DUINO_STATE_MINING,
      DUINO_STATE_SHARE_SUBMITTED,
      DUINO_STATE_SOLVE_FAILED,
      DUINO_STATE_JOB_DONE_SEND,
      DUINO_STATE_JOB_DONE_WAIT,
      DUINO_STATE_TBC
    };

    // config / identity
    String _host = "";
    int _port = 0;
    String _username;
    String _chip_id = "";
    String _miner_name = "";  // arbitrary set name for the worker, Auto - will make one, None - not set, otherwise user defined
    bool _serverConnected = false;
    // The starting diff can be a number but also worker type (AVR | ESP32 | ESP32S (single core))
    String _start_diff = "AVR";
    bool _isMasterMiner = false;
    // network
    WiFiClient _client;

    String _poolMOTD;
    String _poolVersion;

    // state
    enum Duino_State _state = DUINO_STATE_NONE;
    uint32_t  _state_start = 0;
    uint32_t  _last_connect_try = 0;
    bool _is_mining = false;

    // Mining data
    String _seed;
    String _target;
    uint32_t _diff;
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
    unsigned long _startTime = millis();

    unsigned long _clientsConnectTime = 0;
    byte _firstClientAddr = 0;

    // callback
    MinerEventCallback _cb = nullptr;
    void* _cb_user = nullptr;

    void _generate_miner_name();
    void _set_chip_id();
    
    // I/O helpers
    bool _connectIfNeeded();
    bool _sendLine(const String& s);
    bool _readLine(String& out, uint32_t timeout_ms);

    bool _max_micros_elapsed(unsigned long current, unsigned long max_elapsed);
    void _handleSystemEvents();

    // Protocol steps
    bool _handleMotd();
    bool _requestJob();
    bool _recvJobTriplet(String& seed40, String& target40, uint16_t& diff);
    bool _solveAndSubmit(const String& seed40, const String& target40, uint32_t diff);
    uint8_t *_hexStringToUint8Array(const String &hexString, uint8_t *uint8Array, const uint32_t arrayLength);
    bool _handleSubmitResponse();

    inline void _emit(MinerEvent ev, const MinerEventData& d) {
      if (_cb) _cb(ev, d, _cb_user);
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

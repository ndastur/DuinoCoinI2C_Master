#pragma once
#ifndef _POOL_H
#define _POOL_H

#include <WiFiClient.h>
#include <Arduino.h>

enum DeviceType : uint8_t {
  DEVICE_SLAVE,
  DEVICE_AVR,
  DEVICE_ESP32
};

struct Job {
  String prevHash;
  String expectedHash;
  uint16_t difficulty;
};

enum PoolEvent : uint8_t {
  POOLEVT_CONNECTED,        // payload: text = "host:port"
  POOLEVT_DISCONNECTED,     // payload: text = reason
  POOLEVT_MOTD,             // payload: text = motd
  POOLEVT_JOB_REQUESTED,    //
  POOLEVT_JOB_RECEIVED,     // payload: seed40 / target40 / diff
  POOLEVT_RESULT_GOOD,
  POOLEVT_RESULT_BLOCK,
  POOLEVT_RESULT_BAD,
  POOLEVT_ERROR
};

struct PoolEventData {
  // Common text buffer
  const char* text = nullptr;
  const Job *jobDataPtr;
};
// C-style callback to avoid pulling in <functional>
typedef void (*PoolEventCallback)(PoolEvent ev, const PoolEventData& data);

class Pool {
  public:
    Pool(String username, String miningKey, DeviceType type, String workerId = "Auto");

    void setup();     // connect/register with DuinoCoin pool(s)
    void loop();
    bool update();   // fetch new pool info from server
    bool connect();
    bool disconnect();

    // Pool commands
    bool requestMOTD();
    bool requestJob();
    Job* getJob();
    bool submitJob(uint32_t foundNonce, uint32_t elapsedTimeUS);

    bool isConnected();
    void setUsername(String un);
    void setMinerName(String minerName);
    void setWorkerId(String workerId = "Auto");

    void setMiningKey(String new_mining_key);

    // Register a generic event callback
    void onEvent(PoolEventCallback cb);

  private:
    // callback
    PoolEventCallback _cb = nullptr;
    void* _cb_user = nullptr;

    // network
    String _name = "";
    String _host = "";
    int _port = 0;
    String _username;
    String _miningKey = "None";
    DeviceType _type;
    String _minerName = "";
    String _workerId = "";
    String _poolVersion;
    String _MOTD;
    String _startingDifficulty;

    WiFiClient _client;   // not wifi but the TCP client to the pool
    unsigned long _poolConnectTime = 0;
    uint32_t _lastConnectTry = 0;
    uint8_t _tryCount = 0;
    uint32_t lastAttempt = 0;

    // Jobs fifo queue
    // uint8_t jobsFifoRead = 0;
    // uint8_t jobsFifoWrite = 0;
    // Job jobsAVR[5];
    // Job jobsESP[1];
    Job _poolJob;

    // Error
    String _last_err;

    // State Machine
    enum DUINO_POOL_STATE : uint8_t {
      POOL_STATE_NONE,
      POOL_STATE_IDLE,
      POOL_STATE_CONNECT,
      POOL_STATE_VERSION_WAIT,
      POOL_STATE_MOTD_WAIT,
      POOL_STATE_JOB_WAIT,
      POOL_STATE_SUBMITTED
    };

    // state
    enum DUINO_POOL_STATE _state = POOL_STATE_NONE;
    uint32_t  _stateStartMS = 0;

    void _setState(DUINO_POOL_STATE state);
    bool _isStateStuck();

    // I/O helpers
    bool _sendLine(const String& s);
    bool _readLine(String& out, uint32_t timeout_ms);
    bool _splitTripletCSV(const String& s, String& a, String& b, String& c);
    bool _recvJobTriplet(String& seed40, String& target40, uint16_t& diff);
    bool _handleSubmitJobResponse();

    void _checkMiningKey(String new_mining_key, String ducouser);

    inline void _emit(PoolEvent ev, const PoolEventData& d) {
      if (_cb) _cb(ev, d);
    }

    // Helper to emit simple text events
    inline void _emit_text(PoolEvent ev, const char* msg) {
      PoolEventData d; d.text = msg; _emit(ev, d);
    }
    // Helper to emit just event
    inline void _emit_nodata(PoolEvent ev) {
      PoolEventData d; _emit(ev, d);
    }

};

#endif
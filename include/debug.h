/**
 * DEBUG Stuff
 */

#if defined (TEST_FIRST_HASH)
void runDebugHashTest() {
  uint32_t nonce = 0;
  uint32_t elapsed_time = 0;
  SERIALPRINT_LN("Starting testing nonce find ...");
  if ( masterMiner->findNonce(
      String("d860af6413f39bc0b81da43f7de2d0eb4c015b83"),
      String("015929005720943aef1dd22eea0b988e06b1abe1"),
      8200 * 100 + 1, nonce, elapsed_time) ){

      Serial.println("Found the test Nonce - " + String(nonce));
      Serial.println("In " + String(elapsed_time/1000000.0f) + " secs.");
      Serial.println("HR " + String(nonce / (elapsed_time * 0.000001f) ));

      if(nonce == 279490) { Serial.println("Found expected nonce ..."); }
      else { Serial.println("Didn't find expected nonce :( "); }
      }
  else {
      Serial.println("Find failed :()");
  }
}
#endif

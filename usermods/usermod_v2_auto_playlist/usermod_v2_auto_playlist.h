#pragma once

#ifdef WLED_DEBUG
  #ifndef USERMOD_AUTO_PLAYLIST_DEBUG
    #define USERMOD_AUTO_PLAYLIST_DEBUG
  #endif
#endif

#include "wled.h"

class AutoPlaylistUsermod : public Usermod {

  private:
    
    bool initDone = false;
    bool functionality_enabled = false;
    bool silenceDetected = true;
    byte ambientPlaylist = 1;
    byte musicPlaylist = 2;
    int timeout = 60;
    bool autoChange = false;
    byte lastAutoPlaylist = 0;
    unsigned long lastSoundTime = millis()-(timeout*1000)-100;
    unsigned long change_timer = millis();
    unsigned long autochange_timer = millis();
    float avg_volumeSmth = 0;

    uint_fast32_t energy = 0;

    float avg_long_energy = 250;
    float avg_long_lfc = 1000;
    float avg_long_zcr = 500;

    float avg_short_energy = 250;
    float avg_short_lfc = 1000;
    float avg_short_zcr = 500;

    uint_fast32_t vector_energy = 0;
    uint_fast32_t vector_lfc = 0;
    uint_fast32_t vector_zcr = 0;

    uint_fast32_t distance = 0;
    uint_fast32_t distance_tracker = UINT_FAST32_MAX;
    // uint_fast64_t squared_distance = 0;

    unsigned long lastchange = millis();

    int_fast16_t change_threshold = 50; // arbitrary starting point.
    uint_fast16_t change_threshold_change = 0;

    int change_lockout = 1000;    // never change below this number of millis. Ideally 60000/your_average_bpm*beats_to_skip = change_lockout (1000 = skip 2 beats at 120bpm)
    int ideal_change_min = 10000; // ideally change patterns no less than this number of millis
    int ideal_change_max = 20000; // ideally change patterns no more than this number of millis

    std::vector<int> autoChangeIds;
  
    static const char _name[];
    static const char _autoPlaylistEnabled[];
    static const char _ambientPlaylist[];
    static const char _musicPlaylist[];
    static const char _timeout[];
    static const char _autoChange[];
    static const char _change_lockout[];
    static const char _ideal_change_min[];
    static const char _ideal_change_max[];

  public:

    AutoPlaylistUsermod(bool enabled):Usermod("AutoPlaylist", enabled) {
      // noop
    }

    // gets called once at boot. Do all initialization that doesn't depend on
    // network here
    void setup() {
      USER_PRINTLN("AutoPlaylistUsermod");
      initDone = true;
    }

    // gets called every time WiFi is (re-)connected. Initialize own network
    // interfaces here
    void connected() {
      // noop
    }

    void change(um_data_t *um_data) {

      uint8_t *fftResult = (uint8_t*)um_data->u_data[2];

      energy = 0;

      for (int i=0; i < NUM_GEQ_CHANNELS; i++) {
        energy += fftResult[i];
      }
      
      energy *= energy;
      energy /= 10000; // scale down so we get 0 sometimes

      uint8_t lfc = fftResult[0];
      uint16_t zcr = *(uint16_t*)um_data->u_data[11];

      // WLED-MM/TroyHacks: Calculate the long- and short-running averages
      // and the individual vector distances.

      if (volumeSmth > 1.0f) { 

        // softhack007: original code used 0.998f as decay factor
        avg_long_energy  = avg_long_energy  * 0.99f + energy * 0.01f;
        avg_long_lfc     = avg_long_lfc     * 0.99f + lfc    * 0.01f;
        avg_long_zcr     = avg_long_zcr     * 0.99f + zcr    * 0.01f;

        avg_short_energy = avg_short_energy * 0.90f + energy * 0.10f;
        avg_short_lfc    = avg_short_lfc    * 0.90f + lfc    * 0.10f;
        avg_short_zcr    = avg_short_zcr    * 0.90f + zcr    * 0.10f;

        // allegedly this is faster than pow(whatever,2)
        vector_lfc = (avg_short_lfc-avg_long_lfc)*(avg_short_lfc-avg_long_lfc);
        vector_energy = (avg_short_energy-avg_long_energy)*(avg_short_energy-avg_long_energy);
        vector_zcr = (avg_short_zcr-avg_long_zcr)*(avg_short_zcr-avg_long_zcr);

      }

      // distance is linear, squared_distance is magnitude.
      // linear is easier to fine-tune, IMHO.
      distance = vector_lfc + vector_energy + vector_zcr;
      // squared_distance = distance * distance;

      long change_interval = millis()-lastchange;

      if (distance < distance_tracker && change_interval > change_lockout && volumeSmth > 1.0f) {
        distance_tracker = distance;
      }

      // USER_PRINTF("Distance: %3lu - v_lfc: %5lu v_energy: %5lu v_zcr: %5lu\n",(unsigned long)distance,(unsigned long)vector_lfc,(unsigned long)vector_energy,(unsigned long)vector_zcr);

      if ((millis() - change_timer) > ideal_change_min) { // softhack007 same result as "millis() > change_timer + ideal_change_min", but more robust against unsigned overflow

        // Make the analysis less sensitive if we miss the window.
        // Sometimes the analysis lowers the change_threshold too much for
        // the current music, especially after track changes or during 
        // sparse intros and breakdowns.

        if (change_interval > ideal_change_min && distance_tracker <= 100) {
          
          change_threshold_change = distance_tracker-change_threshold;
          change_threshold = distance_tracker;

          if (change_threshold_change > 9999) change_threshold_change = 0; // cosmetic for debug

          if (functionality_enabled)  {
            #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
            USER_PRINTF("--- lowest distance = %4lu - no changes done in %6ldms - next change_threshold is %4u (%4u diff approx)\n", (unsigned long)distance_tracker,change_interval,change_threshold,change_threshold_change);
            #endif
          }

          distance_tracker = UINT_FAST32_MAX;

        }

        change_timer = millis();

      }

      if (distance <= change_threshold && change_interval > change_lockout && volumeSmth > 1.0f) { 

        change_threshold_change = change_threshold-(distance*0.9f);

        if (change_threshold_change < 1) change_threshold_change = 1;

        if (change_interval > ideal_change_max) {
          change_threshold += change_threshold_change;    // make changes more sensitive
        } else if (change_interval < ideal_change_min) {
          change_threshold -= change_threshold_change;    // make changes less sensitive
        } else {
          change_threshold_change = 0;                    // change was within our window, no sensitivity change
        }

        if (change_threshold < 1) change_threshold = 0;   // we need change_threshold to be signed because otherwise this wraps to UINT_FAST16_MAX

        distance_tracker = UINT_FAST32_MAX;

        if (functionality_enabled) {
            
          if (autoChangeIds.size() == 0) {
            if(currentPlaylist < 1) return;

            #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
            USER_PRINTF("Loading presets from playlist: %3d\n", currentPlaylist);
            #endif

            JsonObject playtlistOjb = doc.to<JsonObject>();
            serializePlaylist(playtlistOjb);
            JsonArray playlistArray = playtlistOjb["playlist"]["ps"];

            for(JsonVariant v : playlistArray) {
              #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
              USER_PRINTF("Adding %3u to autoChangeIds\n", v.as<int>());
              #endif
              autoChangeIds.push_back(v.as<int>());
            }

          }

          uint8_t newpreset = 0;

          do {
            newpreset = autoChangeIds.at(random(0, autoChangeIds.size())); // random() is *exclusive* of the last value, so it's OK to use the full size.
          } while ((currentPreset == newpreset) && (autoChangeIds.size() > 1)); // make sure we get a different random preset. Unless there is only one.

          if (change_interval > change_lockout+3) {

            // Make sure we have a statistically significant change and we aren't
            // just bouncing off change_lockout. That's valid for changing the
            // thresholds, but might be a bit crazy for lighting changes. 
            // When the music changes quite a bit, the distance calculation can
            // go into freefall - this logic stops that from triggering right
            // after change_lockout. Better for smaller change_lockout values.

            applyPreset(newpreset);
            
            #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
            USER_PRINTF("*** CHANGE distance = %4lu - change_interval was %5ldms - next change_threshold is %4u (%4u diff aprox)\n",(unsigned long)distance,change_interval,change_threshold,change_threshold_change);
            #endif

          } else {

            #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
            USER_PRINTF("^^^ SKIP!! distance = %4lu - change_interval was %5ldms - next change_threshold is %4u (%4u diff aprox)\n",(unsigned long)distance,change_interval,change_threshold,change_threshold_change);
            #endif

          }

        }
        
        lastchange = millis();  
        change_timer = millis();

      }

    }

    /*
     * Da loop.
     */
    void loop() {
      
      if (!enabled) return;

      if (millis() < 10000) return; // Wait for device to settle

      if (lastAutoPlaylist > 0 && currentPlaylist != lastAutoPlaylist && currentPreset != 0) {
        if (functionality_enabled) {
          #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
          USER_PRINTF("AutoPlaylist: disable due to manual change of playlist from %u to %d, preset:%u\n", lastAutoPlaylist, currentPlaylist, currentPreset);
          #endif
          functionality_enabled = false;
        } else if (currentPlaylist == musicPlaylist) {
          #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
          USER_PRINTF("AutoPlaylist: enabled due to manual change of playlist back to %u\n", currentPlaylist);
          #endif
          functionality_enabled = true;
          lastAutoPlaylist = currentPlaylist;
        }
      }

      if (!functionality_enabled && currentPlaylist == musicPlaylist) {
          #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
          USER_PRINTF("AutoPlaylist: enabled due selecting musicPlaylist(%u)\n", musicPlaylist);
          #endif
          functionality_enabled = true;
      }

      // if (!functionality_enabled) return;

      if (bri == 0) return;

      um_data_t *um_data;

      if (!usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE)) {
        // No Audio Reactive
        silenceDetected = true;
        return;
      }

      float volumeSmth = *(float*)um_data->u_data[0];

      avg_volumeSmth = avg_volumeSmth * 0.99f + volumeSmth * 0.01f;

      if (avg_volumeSmth >= 1.0f) {
        lastSoundTime = millis();
      }

      if (millis() - lastSoundTime > (long(timeout) * 1000)) {
        if (!silenceDetected) {
          silenceDetected = true;
          USER_PRINTLN("AutoPlaylist: Silence detected");
          changePlaylist(ambientPlaylist);
        }
      } else {
        if (silenceDetected) {
          silenceDetected = false;
          USER_PRINTLN("AutoPlaylist: Sound detected");
          changePlaylist(musicPlaylist);
        }
        if (autoChange && millis() >= autochange_timer+22) {
          change(um_data);
          autochange_timer = millis();
        }
      }
    }

    /*
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void addToJsonInfo(JsonObject& root) {
      JsonObject user = root["u"];
      if (user.isNull()) {
        user = root.createNestedObject("u");
      }

      JsonArray infoArr = user.createNestedArray(FPSTR(_name));  // name

      String uiDomString = F("");
      
      if (enabled && functionality_enabled) {
        uiDomString += F("AutoPlaylist is Running");
      } else if (!enabled) {
        uiDomString += F("AutoPlaylist is Disabled");
      } else {
        uiDomString += F("AutoPlaylist is Suspended");
      }

      uiDomString += F("<br />");

      if (enabled && autoChange && currentPlaylist == musicPlaylist && functionality_enabled) {
        uiDomString += F("AutoChange is Active");
      } else if (autoChange && (currentPlaylist != musicPlaylist || !functionality_enabled || !enabled)) {
        uiDomString += F("AutoChange on Stand-by");
      } else if (!autoChange) {
        uiDomString += F("AutoChange is Disabled");
      }

      uiDomString += F("<br />");

      if (currentPlaylist == musicPlaylist && currentPlaylist > 0) {
        uiDomString += F("Playlist: Music Playlist");
      } else if (currentPlaylist == ambientPlaylist && currentPlaylist > 0) {
        uiDomString += F("Playlist: Ambient Playlist");
      } else {
        uiDomString += F("Playlist: Overridden");
      }
      
      // #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
      // uiDomString += F("<br />");
      // uiDomString += F("Change Threshold: ");
      // uiDomString += String(change_threshold);
      // #endif

      infoArr.add(uiDomString);

    }

    /*
     * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void readFromJsonState(JsonObject& root) {
      return;
    }

    void appendConfigData() {
      oappend(SET_F("addHB('AutoPlaylist');"));
    }

    /*
     * addToConfig() can be used to add custom persistent settings to the cfg.json file in the "um" (usermod) object.
     * It will be called by WLED when settings are actually saved (for example, LED settings are saved)
     * If you want to force saving the current state, use serializeConfig() in your loop().
     * 
     * CAUTION: serializeConfig() will initiate a filesystem write operation.
     * It might cause the LEDs to stutter and will cause flash wear if called too often.
     * Use it sparingly and always in the loop, never in network callbacks!
     * 
     * addToConfig() will also not yet add your setting to one of the settings pages automatically.
     * To make that work you still have to add the setting to the HTML, xml.cpp and set.cpp manually.
     * 
     * I highly recommend checking out the basics of ArduinoJson serialization and deserialization in order to use custom settings!
     */
    void addToConfig(JsonObject& root) {

      JsonObject top = root.createNestedObject(FPSTR(_name)); // usermodname

      top[FPSTR(_autoPlaylistEnabled)] = enabled;
      top[FPSTR(_timeout)]             = timeout;
      top[FPSTR(_ambientPlaylist)]     = ambientPlaylist;  // usermodparam
      top[FPSTR(_musicPlaylist)]       = musicPlaylist;    // usermodparam
      top[FPSTR(_autoChange)]          = autoChange;
      top[FPSTR(_change_lockout)]      = change_lockout;
      top[FPSTR(_ideal_change_min)]    = ideal_change_min;
      top[FPSTR(_ideal_change_max)]    = ideal_change_max;

      lastAutoPlaylist = 0;

      #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
      USER_PRINTLN(F("AutoPlaylist config saved."));
      #endif

    }

    /*
     * readFromConfig() can be used to read back the custom settings you added with addToConfig().
     * This is called by WLED when settings are loaded (currently this only happens once immediately after boot)
     * 
     * readFromConfig() is called BEFORE setup(). This means you can use your persistent values in setup() (e.g. pin assignments, buffer sizes),
     * but also that if you want to write persistent values to a dynamic buffer, you'd need to allocate it here instead of in setup.
     * If you don't know what that is, don't fret. It most likely doesn't affect your use case :)
     * 
     * The function should return true if configuration was successfully loaded or false if there was no configuration.
     */
    bool readFromConfig(JsonObject& root) {

      JsonObject top = root[FPSTR(_name)];

      if (top.isNull()) {
        USER_PRINT(FPSTR(_name));
        USER_PRINTLN(F(": No config found. (Using defaults.)"));
        return false;
      }

      enabled          = top[FPSTR(_autoPlaylistEnabled)] | enabled;
      timeout          = top[FPSTR(_timeout)]             | timeout;
      ambientPlaylist  = top[FPSTR(_ambientPlaylist)]     | ambientPlaylist;
      musicPlaylist    = top[FPSTR(_musicPlaylist)]       | musicPlaylist;
      autoChange       = top[FPSTR(_autoChange)]          | autoChange;
      change_lockout   = top[FPSTR(_change_lockout)]      | change_lockout;
      ideal_change_min = top[FPSTR(_ideal_change_min)]    | ideal_change_min;
      ideal_change_max = top[FPSTR(_ideal_change_max)]    | ideal_change_max;

      #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
      USER_PRINT(FPSTR(_name));
      USER_PRINTLN(F(" config (re)loaded."));
      #endif

      // use "return !top["newestParameter"].isNull();" when updating Usermod with new features
      return true;

  }

    /*
     * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
     * This could be used in the future for the system to determine whether your usermod is installed.
     */
    uint16_t getId() {
      return USERMOD_ID_AUTOPLAYLIST;
    }

  private:

    void changePlaylist(byte id) {
        String name = "";
        getPresetName(id, name);
        #ifdef USERMOD_AUTO_PLAYLIST_DEBUG
        USER_PRINTF("AutoPlaylist: Applying \"%s\"\n", name.c_str());
        #endif
        applyPreset(id, CALL_MODE_NOTIFICATION);
        lastAutoPlaylist = id;
    }

};

const char AutoPlaylistUsermod::_name[]                PROGMEM = "AutoPlaylist";
const char AutoPlaylistUsermod::_autoPlaylistEnabled[] PROGMEM = "enabled";
const char AutoPlaylistUsermod::_ambientPlaylist[]     PROGMEM = "ambientPlaylist";
const char AutoPlaylistUsermod::_musicPlaylist[]       PROGMEM = "musicPlaylist";
const char AutoPlaylistUsermod::_timeout[]             PROGMEM = "timeout";
const char AutoPlaylistUsermod::_autoChange[]          PROGMEM = "autoChange";
const char AutoPlaylistUsermod::_change_lockout[]      PROGMEM = "change_lockout";
const char AutoPlaylistUsermod::_ideal_change_min[]    PROGMEM = "ideal_change_min";
const char AutoPlaylistUsermod::_ideal_change_max[]    PROGMEM = "ideal_change_max";

# sprintesp
C++, HTML, JS code for an ESP32 driven sprint time measurement device, utilizing the ESP-IDF framework 

Code directories:
- sd_content = website code to manage the ESP32 during operation
- sprint_startpoint = device without SD slot or website, online http server running in own network
- sprint_endpoint   = device with SD slot and website, accessible by other devices, operating in dual mode
<br/>-> dhcp-server & router of one network
<br/>-> loggs as client into other network, to there perform http requests

Meaning of the LEDs glowing:
- blue blinking: no WiFi connection
- blue steady:   WiFi connection found
- green blinking: sync has been executed
  -> if blinking simultaneously on both devices, successful sync is likely
- red steady: indicator for laser beam pointing at sensor successfully
  -> only works during Laser Alignment mode
     -> reason: aim to reduce complexety of fast-paced value readouts when syncing or measuring

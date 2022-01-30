# sprintesp
C++, HTML, JS code for an ESP32 driven sprint time measurement device, utilizing the ESP-IDF framework 

Code directories:
- sd_content = website code to manage the ESP32 during operation
- sprint_startpoint = device without SD slot or website, online http server running in own network
- sprint_endpoint   = device with SD slot and website, accessible by other devices, operating in dual mode
<br/>-> dhcp-server & router of WiFi network accessible by clients (tablets, laptops, phones) using the web interface
<br/>-> logs as client into network of start point device, towards which it performs http requests to obtain information and give directions

Meaning of the LEDs glowing:
- blue blinking: no WiFi connection
- blue steady:   WiFi connection found
- green blinking: sync has been executed
  -> if blinking simultaneously on both devices, successful sync is likely
- red steady: indicator for laser beam pointing at sensor successfully
  -> only works during Laser Alignment mode
     -> reason: aim to reduce complexety of fast-paced value readouts when syncing or measuring

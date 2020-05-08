# ESP8266 DS18B20 WiFi Temperature Sensor

See also https://www.kaper.com/ for blog post.

OneWire DS18S20, DS18B20, DS1822 Temperature standard example, combined with some code to start a WebServer, and allow for OTA updates.
The web server will respond to a query to the root (/) with the temperature value (celsius) using two decimals behind the comma (dot).
If the sensor does not respond, the temperature returned by the web server will get set to 0.0.

The middle pin of the DS18B20 should be connected to pin D4. The left pin (look at flat side, legs down) must be connected to GND,
and the right pin must be connected to 5V. Normally everyone tells you to put a pull-up resistor of 4K7 between D4 and 5V, but I had to
remove it to get the sensor to work. Strange... Maybe my ESP8266 (D1 mini clone) does not like the 5V to be fed back into the input pin directly?
Or my DS18B20 clone component does not need it. Just try with and without pull-up resistor (try 4K7, without, and 1K) to see what works best.
Initially I soldered my DS18B20 directly to the ESP board, but that immediately increased the temperature to somewhere in the 30 degrees celsius,
while it was just 21 degrees. The ESP board is a bit warmer than the environment sometimes. So I used 20 cm wires in between to fix that issue.

5/5/2020, Thijs Kaper.


# Example run

```
$ curl -s http://192.168.0.57/
28.25
```

# Example telegraf/grafana configuration

You can use telegraf to read the temperature, and send it to an influx database, from which you can make a nice graph using grafana.

Telegraf script: (I have called it "get-fuse-box-temp.sh", as my NAS and Modem and some more equipment are in the warm fuxebox-closet)
```
#!/bin/bash

echo -n "fusebox_temperature degrees_c="
curl -s http://192.168.0.57/
```

telegraf.conf section to execute the script:
```
[[inputs.exec]]
  commands = [ "/etc/telegraf/scripts/get-fuse-box-temp.sh" ]
  data_format = "influx"
```

Example grafana query ($host and $interval come from two dropdown select boxes on the graph page):
```
SELECT mean("degrees_c") FROM "fusebox_temperature" WHERE "host" =~ /^$host$/ AND $timeFilter GROUP BY time($interval) fill(null)
```



# homebridge-http-esp8266-light
HomeKit support for your not so smart lamp.

OK, thats not exactly true. With this, you won't gain any HomeKit functionality without any further steps, but it acts as an interface to [HomeBridge](https://github.com/nfarina/homebridge) ([official website](https://homebridge.io/)). I addition to homebridge, you need the extension [homebridge-http](https://github.com/PeterBrain/homebridge-http) and [homebridge-http-temperature-humidity](https://github.com/PeterBrain/homebridge-http-temperature-humidity).

## Features (what you get)
* HTTP communication
* OTA (Over the Air) Updates
* smooth brightness, hue and saturation changes
* temperature & humidity data
* ability to control RF devices
* offline mode
* wall switch support

## Required hardware (what you need)
* **esp8266 (-12F)** - I bought the WittyCloud from AliExpress, because I was too lazy to solder. I am sure the NodeMCU is supportet too, but you have to take care of the pin mapping
* **led strips** - I went for el chepo ones from ali
* **2x MOSFET** - I believe... not sure which type or specification; found them somewhere in my house
* **power supply** - obvious, but I write it down for documentation
* **some cables** - the bar is very low at the moment
* **breadboard** - bar = still low
* **1x DHT22** - the bar rises; definately from ali again
* **1x 433MHz RF transmitter** - do I have to say where I got them from?
* **1x relay** - this one is not from ali, but I was only a few clicks away from buying

### Costs
| Item                  | Description                     | Price   | LINK                                   |
| :-------------------- | :------------------------------ | ------: | :------------------------------------- |
| esp8266 (WittyCloud)  | NodeMCU is also possible        | ~2.5$   | [LINK](https://www.aliexpress.com/wholesale?SearchText=witty+cloud) |
| LED light strip       | Warm and cold white             | ~5-10$  | [LINK](https://www.aliexpress.com/wholesale?SearchText=light+strip+ww+cw) |
| MOSFET                | don't have the specs right now  | 0$      | [~~LINK~~](https://www.aliexpress.com/wholesale?SearchText=mosfet) |
| DC Power supply       | had one at home (6A?)           | 0$      | [LINK](https://www.aliexpress.com/wholesale?SearchText=12+power+supply) |
| Jumper wires          | had several at home             | 0$      | [LINK](https://www.aliexpress.com/wholesale?SearchText=dupont) |
| Breadboard            | had one at home                 | 0$      | [LINK](https://www.aliexpress.com/wholesale?SearchText=breadboard) |
| DHT22 (AM2302)        |                                 | ~2.5$   | [LINK](https://www.aliexpress.com/wholesale?SearchText=dht22+am2302) |
| 433Mhz RF transmitter |                                 | ~0.5$   | [LINK](https://www.aliexpress.com/wholesale?SearchText=rf+module) |
| Relay                 |                                 | 0$      | [LINK](https://www.aliexpress.com/wholesale?SearchText=relay) |
|                       | Total                           | ~15.5$  |                                        |

The total will be higher if you don't have some parts laying around. Remember: AliExpress is your friend, but **don't** expect it to be delivered **within one month**.

I made this documentation for myself, not because I need it, just because I was bored and had nothing else to do.

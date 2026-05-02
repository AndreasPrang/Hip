# Laura Dino Run

Kleines Geometry-Dash-aehnliches Jump-and-Dodge-Spiel fuer:

- ESP32-C5
- GME12864-80 128x64 OLED ueber I2C
- einen Taster mit optionaler LED

Das Projekt ist ein ESP-IDF-Projekt ohne externe Libraries. Das OLED wird direkt ueber I2C angesteuert.

## Spiel

- Beim Einschalten laeuft ein animierter Splash Screen.
- Ein kurzer Tastendruck startet das Spiel.
- Jeder Tastendruck laesst die Spielfigur springen.
- Ein langer Tastendruck im Startscreen oder nach Game Over oeffnet das Menue.
- Im Menue geht ein kurzer Tastendruck zum naechsten Eintrag, ein langer Tastendruck bestaetigt.
- Der Menuepunkt `OFF` schaltet OLED und ESP32-C5 in Deep Sleep.
- Ein weiterer Tastendruck weckt den ESP32-C5 wieder auf.
- Unter `Games` gibt es `Laura Dino Run` und `Tiny Copter`.
- In `Tiny Copter` fliegt der Hubschrauber beim Gedrueckthalten hoch und faellt beim Loslassen runter.
- Highscores werden getrennt pro Spiel und Schwierigkeit gespeichert.
- Hindernisse kommen von rechts nach links.
- Wenn die Figur ein Hindernis beruehrt, erscheint `GAME OVER`.
- Danach startet ein weiterer Tastendruck neu.
- Der Highscore wird im NVS-Speicher des ESP32-C5 gespeichert und bleibt nach Reset/Power-Off erhalten.
- Die Button-LED leuchtet beim Druecken, beim Springen und blinkt nach Game Over.

## Standard-Pins

Diese Werte sind nur Startwerte und muessen eventuell zu deinem ESP32-C5-Board passen:

| Signal | GPIO |
| --- | ---: |
| OLED SDA | 4 |
| OLED SCL | 5 |
| Button | 6 |
| Button LED | 8 |

Der Button ist standardmaessig `active low`: eine Seite an GPIO, die andere an GND. Der interne Pull-up wird aktiviert.
Fuer Deep Sleep Wakeup muss der Button auf einem RTCIO-Pin des ESP32-C5 liegen. Praktisch sind GPIO `0..6`; GPIO `9` funktioniert als normaler Button, kann den ESP32-C5 aber nicht aus Deep Sleep wecken.

## OLED

Standardadresse ist `0x3C`. Falls dein Display nicht reagiert, pruefe:

- I2C-Adresse `0x3C` oder `0x3D`
- SDA/SCL-Pins
- Versorgungsspannung und GND
- `OLED column offset`: `0` fuer viele SSD1306-Module, `2` fuer manche SH1106-kompatible Module

## Build und Flash

ESP-IDF muss installiert und aktiviert sein.

Der ESP32-C5 wird ueber UART/USB-Serial geflasht. Auf macOS findest du den Port mit:

```bash
ls /dev/cu.*
```

In dieser Umgebung ist aktuell z. B. dieser Port sichtbar:

```text
/dev/cu.usbmodem5B5F0282801
```

Wenn dein Board einen anderen UART-Adapter nutzt, ersetze den Port in den Befehlen.

```bash
cd "/Users/andreas.prang/Library/Mobile Documents/com~apple~CloudDocs/Documents/Development/Laura/Hip"
idf.py set-target esp32c5
idf.py menuconfig
idf.py build
idf.py -p /dev/cu.usbmodem5B5F0282801 flash monitor
```

Falls das Board nicht automatisch in den Bootloader wechselt, halte beim Start des Flashens `BOOT` gedrueckt, druecke kurz `RESET`, und lasse `BOOT` los, sobald `Connecting...` erscheint.

Die Hardwarewerte findest du in `menuconfig` unter:

```text
Laura Dino Run hardware
```

## Wichtige Dateien

- `main/main.c`: Spiel, Grafik, OLED-I2C-Treiber, Button-Handling
- `main/Kconfig.projbuild`: einstellbare Pins, OLED-Adresse und Column-Offset
- `sdkconfig.defaults`: kleine Projektdefaults

## Naechste sinnvolle Erweiterungen

- Sound/Buzzer fuer Sprung und Kollision
- mehrere Hindernisformen
- Kalibrierbarer Schwierigkeitsgrad

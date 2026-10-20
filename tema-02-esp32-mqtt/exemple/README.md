# Exemple: laboratoarele ESP32-C3

Exemplele pentru această temă nu sunt în acest repository, ci în repository-ul cu laboratoare ESP32:

**<https://github.com/automatica-cluj/rt-labs-esp32c3>**

Repository-ul conține un laborator introductiv (`lab0-arduino-basics`) și cinci laboratoare de timp real pe ESP32-C3 Super Mini. Pentru temă, folosiți-l **ca sursă de exemple de cod**:

- **nu** trebuie să parcurgeți laboratoarele și nici să urmați pas cu pas toate instrucțiunile din ele;
- vă interesează **exemplele din laboratorul 0** (secțiunea [6](#6-ce-folosiți-din-laboratorul-0)); pe baza lor scrieți taskurile temei;
- din restul repository-ului vă ajută doar câteva referințe punctuale despre FreeRTOS (secțiunea [7](#7-referințe-despre-freertos)).

Laboratoarele sunt scrise în engleză.

## 1. Clonarea

Clonați repository-ul **în afara** fork-ului `rt-homeworks`, de exemplu alături de el:

```bash
cd ~/ssatr                    # folderul în care aveți și fork-ul rt-homeworks
git clone https://github.com/automatica-cluj/rt-labs-esp32c3
```

Nu copiați laboratoarele în fork-ul temei: acolo intră doar soluția voastră.

## 2. Configurarea Arduino IDE

Se face o singură dată și durează în jur de 20 de minute, mai ales din cauza descărcărilor. Pașii compleți sunt în [`docs/setup.md`](https://github.com/automatica-cluj/rt-labs-esp32c3/blob/main/docs/setup.md) din repository-ul cu laboratoare. Pe scurt:

1. Instalați **Arduino IDE 2** de pe [arduino.cc](https://www.arduino.cc/en/software).
2. *File > Preferences > Additional boards manager URLs*, adăugați:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. *Tools > Board > Boards Manager*, căutați **esp32** și instalați **esp32 by Espressif Systems**, versiunea **3.3.x**. Versiunile 2.x au alt API și nu compilează exemplele.
4. Conectați placa cu un cablu **USB-C de date** și setați în meniul *Tools*:

| Setare | Valoare |
|--------|---------|
| Board | **ESP32C3 Dev Module** |
| Port | portul nou apărut (`COMx`, `/dev/cu.usbmodem...`, `/dev/ttyACM0`) |
| USB CDC On Boot | **Enabled** (altfel nu apare nimic în Serial Monitor) |
| CPU Frequency | 160 MHz (implicit) |
| restul | valorile implicite |

Placa nu are un cip separat de conversie USB-serial: ESP32-C3 vorbește direct USB. De aceea nu trebuie instalat niciun driver pe Windows sau macOS, dar setarea *USB CDC On Boot* este obligatorie, pentru că ea trimite `Serial` pe portul USB.

Pe **Linux**, adăugați-vă în grupul `dialout`, ca să aveți acces la port: `sudo usermod -aG dialout $USER`, apoi delogare și logare.

## 3. Prima încărcare

1. Deschideți `lab0-arduino-basics/hello_serial/hello_serial.ino`.
2. Apăsați *Upload* (săgeata). Prima compilare durează câteva minute.
3. Deschideți *Tools > Serial Monitor*, la **115200 baud**.
4. Apăsați butonul **RST** de pe placă. Trebuie să apară `=== lab 0: hello_serial ===` și meniul de taste.

Ca să trimiteți o tastă schiței, o scrieți în linia de intrare din Serial Monitor și apăsați Enter.

## 4. Probleme frecvente

| Problemă | Ce faceți |
|----------|-----------|
| nu apare niciun port nou când conectați placa | cablul este, probabil, doar de încărcare; încercați alt cablu USB-C |
| Serial Monitor rămâne gol | verificați *USB CDC On Boot: Enabled* și 115200 baud, apoi apăsați RST |
| *Upload* se blochează la „Connecting...” sau portul dispare | puneți placa în modul bootloader: țineți apăsat **BOOT**, apăsați și eliberați **RST**, eliberați **BOOT**; apoi *Upload* din nou și, la final, RST |
| placa nu pornește schița după RST | nu țineți BOOT apăsat în timp ce apăsați RST sau conectați cablul: așa porniți bootloaderul |
| placa repornește singură, cu `Stack canary watchpoint triggered` sau `Guru Meditation Error` | un task are stiva prea mică sau accesează memorie greșit; măriți stiva din `xTaskCreate` (minim 4096) |
| Wi-Fi nu se conectează | ESP32-C3 merge **doar pe 2.4 GHz**; verificați numele și parola (contează literele mari și mici) |
| hotspot de pe telefon nevăzut de placă | iPhone: *Personal Hotspot > Maximize Compatibility*; Android: banda hotspot-ului pe 2.4 GHz |
| Wi-Fi conectat, dar MQTT dă eroare | unele rețele blochează portul 11188; folosiți rețeaua `iot-demo` din laborator sau hotspot-ul telefonului |
| MQTT se conectează și se deconectează la nesfârșit | alt client folosește același client id; faceți-l unic (de exemplu cu numele vostru și ID-ul device-ului) |

O schiță care se blochează sau ține procesorul ocupat poate „lua” cu ea portul USB. Placa nu s-a stricat: procedura cu BOOT + RST din tabel o readuce mereu.

## 5. Placa ESP32-C3 Super Mini

| | |
|---|---|
| Procesor | un nucleu RISC-V, 160 MHz |
| RAM | 400 KB (în jur de 320 KB liberi pentru schiță) |
| Wi-Fi | 2.4 GHz (fără 5 GHz) |
| LED albastru | GPIO 8, aprins când pinul este **LOW** |
| Butonul BOOT | GPIO 9, citește **LOW** cât timp este apăsat |
| Senzor de temperatură | intern, al cipului: `temperatureRead()` |

Pinii 2, 8 și 9 sunt pini de „strapping”: nivelul lor la reset decide cum pornește cipul. Nu legați la ei nimic care îi trage puternic spre un nivel.

## 6. Ce folosiți din laboratorul 0

Citiți [`lab0-arduino-basics/README.md`](https://github.com/automatica-cluj/rt-labs-esp32c3/blob/main/lab0-arduino-basics/README.md) și rulați schițele de mai jos. Fiecare are un comentariu la început care explică ce face.

| Schița | Ce găsiți în ea | Unde vă folosește în temă |
|--------|-----------------|---------------------------|
| `hello_serial` | structura `setup()` / `loop()`, `Serial.begin()`, așteptarea Serial Monitor-ului la pornire, `Serial.printf()`, tipurile C pe acest cip (`uint32_t`, `int64_t`) | mesajele de diagnostic pe Serial; tipărirea timestamp-ului pe 64 de biți |
| `blink` | LED-ul (activ pe LOW), `millis()` vs `delay()`: de ce o așteptare blocantă oprește tot restul programului | înțelegeți de ce fiecare activitate primește propriul task FreeRTOS: acolo, o așteptare blochează doar taskul care așteaptă |
| `mqtt_hello` | conectarea la Wi-Fi, clientul MQTT din ESP-IDF (fără biblioteci de instalat), care rulează în propriul task FreeRTOS, handlerul de evenimente, abonarea la fiecare conectare, publicarea, citirea unui mesaj primit | baza pentru `connection.cpp` din schelet: citiți-l ca să înțelegeți conexiunea pe care o primiți gata făcută și de ce `command_received()` rulează pe alt task decât taskurile voastre |
| `button_poll`, `pwm_fade` | butonul BOOT, PWM | nu sunt necesare pentru temă |

Ce diferă `connection.cpp` din schelet față de `mqtt_hello`:

| `mqtt_hello` | Tema 02 (`connection.cpp`) |
|--------------|---------|
| topic-uri `rt-labs/<id>/uptime` și `rt-labs/<id>/led` | topic-uri `ssatr/<student>/devices/<DEVICE_ID>/...`, construite cu `make_topic()` din `config.h` |
| client id `rt-labs-<id>` | client id unic, de exemplu `<STUDENT>-<DEVICE_ID>` |
| QoS 0 | QoS 1, ca în tema 01 |
| `loop()` publică o dată pe secundă, cu `delay()` | `mqtt_publish()`, apelată din taskurile voastre; `loop()` nu mai face nimic |
| mesajul primit aprinde LED-ul direct în handler | handlerul copiază textul (cu `'\0'`) și apelează `command_received()`, care îl pune în coadă; comanda o execută taskul aperiodic |
| — | după Wi-Fi, sincronizarea orei prin SNTP (`configTime`, `getLocalTime`) |

## 7. Referințe despre FreeRTOS

Laboratorul 0 nu creează taskuri FreeRTOS proprii. Pentru taskuri, priorități, `xTaskDelayUntil` și cozi, de care aveți nevoie la TODO 0 … 3, uitați-vă doar la:

- [`docs/freertos_concepts.md`](https://github.com/automatica-cluj/rt-labs-esp32c3/blob/main/docs/freertos_concepts.md): taskuri și priorități, taskurile pornite de sistem (cu prioritățile lor), tick-ul, cozile;
- `lab1-time-latency/periodic_jitter/periodic_jitter.ino`: un task periodic cu `xTaskCreate` și `xTaskDelayUntil`, comparat cu varianta cu așteptare relativă.

Schițele din laboratoarele 1–5 includ `rt.h`, un fișier ajutător al laboratoarelor. Tema nu are nevoie de el: folosiți direct funcțiile FreeRTOS (`xTaskCreate`, `xTaskDelayUntil`, `xQueueCreate`, `xQueueSend`, `xQueueReceive`).

## 8. Urmărirea mesajelor MQTT

Până când business-service-ul este gata, verificați placa direct cu un client MQTT: **MQTTX** (grafic) sau `mosquitto_sub` / `mosquitto_pub`. Înlocuiți `popescu-ion` cu numele vostru și `esp32-a1b2c3` cu ID-ul plăcii (prima linie din Serial Monitor):

```bash
# tot traficul vostru
mosquitto_sub -h control.aut.utcluj.ro -p 11188 -t 'ssatr/popescu-ion/#' -v

# o comandă trimisă direct plăcii
mosquitto_pub -h control.aut.utcluj.ro -p 11188 -t 'ssatr/popescu-ion/devices/esp32-a1b2c3/commands' -m 'STATUS'
```

Brokerul este partajat și nu are parolă: oricine îl folosește vă poate citi și scrie topic-urile. Nu trimiteți pe el nimic privat.

## 9. Linie de comandă (opțional)

Tot ce face Arduino IDE se poate face și cu `arduino-cli`, inclus în Arduino IDE sau instalabil separat:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc start/esp32_device
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc -p /dev/ttyACM0 start/esp32_device
arduino-cli monitor -p /dev/ttyACM0 --config baudrate=115200
```

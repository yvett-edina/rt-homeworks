# Tema 02 – Device IoT pe ESP32-C3 cu taskuri FreeRTOS

**Tehnologii:** ESP32-C3 Super Mini, Arduino IDE, FreeRTOS, MQTT, Java 17+ (business-service)
**Termen de predare:** _de stabilit_

## Obiective

După rezolvarea temei, veți putea să:

- implementați taskurile **periodic**, **sporadic** și **aperiodic** din tema 01 pe un microcontroller, ca taskuri **FreeRTOS**;
- folosiți mecanismele FreeRTOS pentru timp și comunicare: **așteptarea absolută** (`xTaskDelayUntil`) și **cozile** (`xQueueSend` / `xQueueReceive`);
- conectați un dispozitiv real la sistemul din tema 01, fără să modificați serviciul care îl folosește.

## Ce se schimbă față de tema 01

În tema 01, device-ul era simulat de o aplicație Java. În această temă îl înlocuiți cu o placă **ESP32-C3 Super Mini**, care face exact același lucru: citește un senzor de temperatură, publică telemetria, trimite alarme și execută comenzi. **Business-service-ul rămâne neschimbat.**

```mermaid
flowchart LR
    subgraph DEV["ESP32-C3 Super Mini (FreeRTOS)"]
        P["Task periodic<br/>citire senzor"]
        S["Task sporadic<br/>alarme"]
        A["Task aperiodic<br/>comenzi"]
        P -- "coadă de alarme" --> S
    end

    B(("Broker MQTT<br/>control.aut.utcluj.ro:11188"))

    subgraph SRV["business-service (Java, din tema 01)"]
        M["Procesare mesaje"]
        CSV[("Fișiere CSV")]
        C["Consolă operator"]
        M --> CSV
    end

    P -- telemetry --> B
    S -- alarm --> B
    A -- status --> B
    B -- "telemetry / alarm / status" --> M
    C -- commands --> B
    B -- commands --> A
```

Ca business-service-ul să funcționeze fără modificări, placa trebuie să respecte **exact protocolul din tema 01**: aceleași topic-uri, același format al mesajelor, aceleași comenzi. Pentru business-service, placa este doar încă un device, cu ID-ul format din `esp32-` și ultimele 6 cifre hexa ale adresei MAC a plăcii, de exemplu `esp32-a1b2c3`. ID-ul se citește singur la pornire și apare pe prima linie din Serial Monitor. Puteți rula în paralel și `device-simulator` din tema 01: dashboard-ul va afișa ambele device-uri.

## De la thread-uri Java la taskuri FreeRTOS

Pe ESP32, Arduino rulează peste **FreeRTOS**, un sistem de operare de timp real. Acesta oferă direct mecanismele pe care în Java le-ați simulat: taskuri cu priorități fixe, planificare preemptivă și așteptare absolută pe grila de eliberări. Logica temei rămâne aceeași; se schimbă doar uneltele:

| Tema 01 (Java) | Tema 02 (ESP32, FreeRTOS) |
|----------------|---------------------------|
| `new Thread(task).start()` | `xTaskCreate(functie, "nume", 4096, NULL, prioritate, NULL)` |
| `run()` cu buclă `while (true)` | funcția taskului, cu buclă `for (;;)`; nu are voie să se termine |
| așteptare absolută: `next += periodMs`, apoi așteptare până la `next` (`System.nanoTime()`) | `xTaskDelayUntil(&last, pdMS_TO_TICKS(T))`: așteaptă până la `last + T` și actualizează `last` |
| `BlockingQueue<Double>`, `put()` / `take()` | `QueueHandle_t`, `xQueueSend()` / `xQueueReceive(..., portMAX_DELAY)` |
| callback-ul Paho pune mesajul în coada `commands` | clientul MQTT din schelet apelează `command_received(text)`, care pune comanda în coada de comenzi |
| `MqttConnection.publish(topic, text)` | `mqtt_publish("telemetry", text)` din schelet |
| `System.currentTimeMillis()` | `epoch_ms()` din schelet, corect după sincronizarea orei prin SNTP |
| `System.nanoTime()` (timp monoton) | `millis()` sau `xTaskGetTickCount()` |
| `volatile` pentru perioadă și prag | `volatile` (variabile globale, citite și modificate din taskuri diferite) |

Diferența importantă față de Java: aici **prioritățile contează**. ESP32-C3 are un singur nucleu, iar planificatorul FreeRTOS rulează mereu taskul gata de execuție cu prioritatea cea mai mare. Alegeți prioritățile celor trei taskuri între 2 și 19: peste `loopTask` (1), dar sub taskurile sistemului care se ocupă de evenimente și de Wi-Fi (20 și 23).

## Resurse puse la dispoziție

### `exemple/`

Un singur [`README.md`](exemple/README.md), cu:

- legătura către repository-ul cu laboratoare ESP32 [`rt-labs-esp32c3`](https://github.com/automatica-cluj/rt-labs-esp32c3), din care vă interesează în primul rând **exemplele din laboratorul 0** (`lab0-arduino-basics`);
- pașii de configurare a Arduino IDE și a plăcii, plus problemele frecvente la prima conectare.

Repository-ul cu laboratoare este o **sursă de exemple de cod**. Nu trebuie să parcurgeți laboratoarele pas cu pas. Citiți și rulați exemplele din laboratorul 0 înainte să scrieți taskurile. Conexiunea (Wi-Fi, MQTT) este gata în schelet, construită pornind de la `mqtt_hello`; pentru taskuri și cozi, `exemple/README.md` indică și câteva referințe despre FreeRTOS.

### `start/esp32_device/`

Scheletul schiței Arduino. Partea de comunicație este gata: Serial, Wi-Fi, sincronizarea orei prin SNTP și clientul MQTT. Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe Serial fiecare comandă primită. **Taskurile și cozile le scrieți voi.** Încărcați scheletul pe placă înainte să scrieți cod și trimiteți-i o comandă (vezi [`exemple/README.md`](exemple/README.md#8-urmărirea-mesajelor-mqtt)), ca să verificați că placa ajunge la broker.

| Fișier | Ce conține |
|--------|------------|
| `config.h` | rețeaua Wi-Fi, brokerul, `STUDENT`, `DEVICE_ID` (citit din adresa MAC), valorile inițiale (perioada, pragul, intervalul minim între sosiri) și `make_topic()`, care construiește numele topic-urilor |
| `connection.h`, `connection.cpp` | gata implementate, nu trebuie modificate (echivalentul `MqttConnection` din tema 01): `wifi_connect()`, `time_sync()`, `epoch_ms()` (timestamp-ul), `mqtt_start()`, `mqtt_publish(kind, text)`; comenzile primite sunt trimise funcției `command_received(text)` |
| `esp32_device.ino` | aici lucrați: `read_temperature()` (senzorul intern), descrierea celor trei taskuri, `command_received()` și `setup()`, cu `TODO 0` … `TODO 3` |

Deschideți `esp32_device.ino` în Arduino IDE (folderul și fișierul `.ino` au același nume, cum cere Arduino IDE). Setările plăcii sunt în [`exemple/README.md`](exemple/README.md#2-configurarea-arduino-ide).

### `start/business-service/`

Proiectul din tema 01, copiat neschimbat, cu `TODO 4` … `TODO 6`. **Copiați aici rezolvarea voastră din tema 01** (`MessageProcessor`, `Dashboard`, `CommandConsole`) și setați `STUDENT` în `Config.java`.

```bash
cd start/business-service
mvn -q compile exec:java
```

## Senzorul de temperatură

Placa nu are un senzor extern. Se folosește **senzorul intern al ESP32-C3**, citit cu `temperatureRead()` (în schelet: `read_temperature()`). Acesta măsoară temperatura cipului, nu a camerei:

- valoarea este mai mare decât temperatura camerei: imediat după conectarea la Wi-Fi poate fi în jur de 50 °C, apoi scade în câteva minute și se stabilizează, de obicei între 35 și 40 °C;
- rezoluția este de aproximativ 1 °C, deci valorile publicate sar din grad în grad (`36.80`, `35.80`, ...);
- odată stabilizată, variază puțin, deci alarmele nu apar singure, ca în tema 01.

Cu pragul inițial de 45 °C, alarmele apar doar în primele secunde după pornire. Ca să provocați alarme mai târziu, urmăriți telemetria și trimiteți un prag cu un grad sub valoarea citită, de exemplu `esp32-a1b2c3 SET_THRESHOLD 35` (cu ID-ul plăcii voastre). Temperatura publicată trece atunci de prag la fiecare job, iar taskul sporadic trebuie să respingă evenimentele care sosesc mai des decât `MIN_INTERARRIVAL_MS`.

## Brokerul MQTT, topic-urile și mesajele

Totul este ca în [tema 01](../tema-01-iot-mqtt/README.md#brokerul-mqtt): brokerul `control.aut.utcluj.ro`, portul `11188`, fără autentificare, și topic-urile sub `ssatr/<student>/`. Pe ESP32, adresa brokerului se scrie ca URI: `mqtt://control.aut.utcluj.ro:11188`.

| Topic | Direcție | Tip task | Conținut |
|-------|----------|----------|----------|
| `ssatr/<student>/devices/esp32-a1b2c3/telemetry` | device → service | periodic | `esp32-a1b2c3;timestamp;temperatura` |
| `ssatr/<student>/devices/esp32-a1b2c3/alarm` | device → service | sporadic | `esp32-a1b2c3;timestamp;temperatura;prag` |
| `ssatr/<student>/devices/esp32-a1b2c3/commands` | service → device | aperiodic | `SET_INTERVAL 1000` |
| `ssatr/<student>/devices/esp32-a1b2c3/status` | device → service | aperiodic | `esp32-a1b2c3;timestamp;OK SET_INTERVAL 1000` |

Comenzile pe care trebuie să le accepte device-ul:

| Comandă | Efect |
|---------|-------|
| `SET_INTERVAL <ms>` | schimbă perioada $T$ a taskului periodic |
| `SET_THRESHOLD <valoare>` | schimbă pragul de alarmă |
| `RESET` | readuce perioada și pragul la valorile inițiale (`PERIOD_MS_INITIAL`, `THRESHOLD_INITIAL`) |
| `STATUS` | răspunde cu perioada și pragul curente |

Senzorul intern nu poate fi „resetat”, de aceea comanda `RESET` are alt efect decât în tema 01.

### Detalii la care trebuie să fiți atenți pe ESP32

- **Timestamp-ul** este, ca în Java, numărul de milisecunde de la 1 ianuarie 1970. Placa nu are ceas de timp real: la pornire nu știe ora, iar scheletul o sincronizează prin **SNTP** (`time_sync()`). Valoarea dată de `epoch_ms()` are 64 de biți și se tipărește cu `%lld`: `(long long)epoch_ms()`.
- **Numerele zecimale** se scriu cu punct: `snprintf(..., "%.2f", t)` pune mereu punct, deci business-service-ul le poate citi cu `Double.parseDouble`.
- **`command_received(text)`** rulează pe taskul clientului MQTT, nu pe taskurile voastre. În ea doar puneți comanda în coadă, fără să vă blocați, și nu o executați: execuția este jobul taskului aperiodic.
- **`text` este valid doar în timpul apelului** `command_received()`. `xQueueSend` copiază elementul cozii, deci elementul trebuie să conțină textul însuși, de exemplu un `struct` cu un câmp `char text[COMMAND_MAX_LEN]`, nu un pointer. La fel, evenimentul de alarmă se pune în coadă prin valoare (de exemplu un `struct` cu temperatura și pragul).
- **Ordinea din `setup()`**: cozile se creează înainte de `mqtt_start()`, pentru că de acolo încolo pot sosi comenzi, iar `command_received()` le pune în coadă. Taskurile se pornesc după `mqtt_start()`, altfel primele mesaje publicate se pierd.
- **Stiva taskurilor**: pe ESP32, dimensiunea din `xTaskCreate` este în octeți. `snprintf` cu numere zecimale are nevoie de stivă, deci folosiți cel puțin 4096. O stivă prea mică repornește placa, cu un mesaj `Stack canary watchpoint triggered` în Serial Monitor.
- **Serial Monitor** este singura voastră fereastră spre placă: afișați pe `Serial` ce face fiecare task (job pornit, alarmă respinsă, comandă executată).
- **`Serial.setTxTimeoutMs(0)`** din `setup()` rămâne pe loc. Fără el, cu placa la USB dar fără Serial Monitor deschis, fiecare `Serial.printf()` blochează taskul până la 2 s: taskul periodic ratează eliberări, iar telemetria nu iese mai des de o dată la 2 s, oricare ar fi perioada.

## Cerințe

### 1. Device-ul ESP32 (`start/esp32_device/`)

- **TODO 0 – structura aplicației** (`setup()`): crearea celor două cozi (`xQueueCreate`), înainte de `mqtt_start()`, și pornirea celor trei taskuri (`xTaskCreate`), după `mqtt_start()`, cu prioritățile alese de voi. În tema 01, partea aceasta era dată gata (`DeviceSimulator`).
- **TODO 1 – task periodic**: la fiecare job citește senzorul și publică telemetria. Eliberările respectă grila, cu perioada $T$ (inițial `PERIOD_MS_INITIAL`). Folosiți **așteptare absolută** (`xTaskDelayUntil`), nu relativă (`delay`, `vTaskDelay`), ca să nu apară derivă. Când temperatura depășește pragul, jobul pune un eveniment în coada de alarme, fără să se blocheze (timeout 0).
- **TODO 2 – task sporadic**: preia evenimentele din coada de alarme, publică alarmele și impune intervalul minim între sosiri `MIN_INTERARRIVAL_MS`: evenimentele care sosesc mai des sunt respinse.
- **TODO 3 – task aperiodic**: `command_received()` pune comenzile în coada de comenzi, iar taskul le preia, execută comenzile din tabelul de mai sus și publică o confirmare pe topic-ul `status`. O comandă greșită produce un mesaj `ERROR ...` și nu oprește taskul.

### 2. Serviciul business (`start/business-service/`)

- Rezolvarea voastră din tema 01 (`TODO 4` … `TODO 6`), adusă în acest proiect. Serviciul trebuie să afișeze în dashboard device-ul plăcii voastre (`esp32-...`), să-i salveze mesajele în fișierele CSV și să-i trimită comenzi.

### 3. Funcționarea completă

Sistemul trebuie să funcționeze cap-coadă: placa publică telemetria cu perioada cerută, alarmele apar când temperatura trece de prag (cel mult una la `MIN_INTERARRIVAL_MS`), iar toate cele patru comenzi trimise din consola business-service-ului sunt executate și confirmate.

## Predare

Soluția se află în fork-ul vostru, în folderul `tema-02-esp32-mqtt/`:

```
tema-02-esp32-mqtt/
└── start/
    ├── esp32_device/         # schița completată
    └── business-service/     # proiectul completat (rezolvarea din tema 01)
```

**Nu urcați în fork parola rețelei voastre Wi-Fi.** Înainte de commit, puneți înapoi în `config.h` valorile pentru `iot-demo`.

După push, adăugați commit ID-ul soluției (`git rev-parse HEAD`) ca **comentariu** la assignment-ul temei din **Microsoft Teams** și apăsați **Turn in**. Detalii în [README-ul principal](../README.md#predare).

## Bibliografie

- Cursul: [2.3 Eliberare periodică](https://control.aut.utcluj.ro:8030/part-02-time/03-periodic-release/), [3.2 Modelul de task](https://control.aut.utcluj.ro:8030/part-03-scheduling/02-task-model/)
- [rt-labs-esp32c3](https://github.com/automatica-cluj/rt-labs-esp32c3): laboratorul 0, `docs/freertos_concepts.md`
- [FreeRTOS: `xTaskDelayUntil`](https://www.freertos.org/Documentation/02-Kernel/04-API-references/02-Task-control/03-xTaskDelayUntil), [cozi](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues)
- [ESP-IDF: clientul MQTT](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/protocols/mqtt.html), [ora sistemului și SNTP](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/system_time.html)
- [Documentația Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/)

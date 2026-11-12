// Semafor pentru pietoni, cu buton, pe ESP32-C3 Super Mini.
//
// Placa este controlerul semaforului; lămpile sunt desenate de dashboard, care
// le aprinde după mesajele "lights" publicate de placă. Patru activități:
//
//   ISR buton BOOT ----+
//                      +--(coada butonului)--> task sporadic
//   command_received --+   (BUTTON)                 |
//                                                   v
//                                         [ starea semaforului ] <-- mutex
//                                            ^          |
//                                            |          v
//                         task periodic de control   task periodic de ieșire
//                         (fazele, T = 100 ms)       (lămpile, T = 500 ms) --> lights
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, pinii, perioadele, duratele fazelor
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Restul îl scrieți voi: TODO 0 ... TODO 3.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"
#include <cstring>

// Fazele semaforului (vezi tabelul și diagrama din README.md). Numele din
// PHASE_NAMES sunt cele din mesajul "lights", în aceeași ordine ca în enum.
enum Phase { CAR_GREEN, CAR_YELLOW, ALL_RED, PED_GREEN };
const char* const PHASE_NAMES[] = {"CAR_GREEN", "CAR_YELLOW", "ALL_RED", "PED_GREEN"};

// Culorile unei lămpi, cu numele din mesajul "lights".
enum Light { RED, YELLOW, GREEN };
const char* const LIGHT_NAMES[] = {"RED", "YELLOW", "GREEN"};

// TODO 0 (c): STAREA SEMAFORULUI, partajată de taskuri și protejată de un
//   mutex (xSemaphoreCreateMutex). De exemplu un struct cu:
//   - faza curentă
//   - momentul intrării în faza curentă (millis()), din care se calculează
//     cât a durat faza și cât mai are
//   - cererea pietonului (lampa AȘTEPTAȚI): setată de taskul sporadic,
//     ștearsă de taskul de control la intrarea în PED_GREEN
//   Mutexul se ține cât mai puțin: în el doar citiți sau scrieți câmpurile
//   (eventual copiați starea într-o variabilă locală). Fără mqtt_publish(),
//   Serial sau așteptări cât timp țineți mutexul.

struct SemaphoreState {
    Phase currentPhase;
    uint32_t phaseStartMs;
    bool pedestrianRequest;
};

SemaphoreState semaphoreState;

SemaphoreHandle_t stateMutex = nullptr;

struct ButtonEvent {
    uint32_t arrivalMs;
};

QueueHandle_t buttonQueue = nullptr;

// TODO 1: TASK PERIODIC DE CONTROL, cu perioada CONTROL_PERIOD_MS.
//   La fiecare job, cu mutexul luat, decide dacă faza curentă s-a terminat și
//   trece în faza următoare, după tabelul din README.md:
//     CAR_GREEN  -> CAR_YELLOW  după CAR_GREEN_MIN_MS, doar dacă există o cerere
//     CAR_YELLOW -> ALL_RED     după CAR_YELLOW_MS
//     ALL_RED    -> PED_GREEN   după ALL_RED_MS; aici se șterge cererea pietonului
//     PED_GREEN  -> CAR_GREEN   după PED_GREEN_MS
//   La fiecare schimbare de fază notați momentul intrării în faza nouă.
//   Taskul nu publică nimic: doar actualizează starea. După ce eliberează
//   mutexul, afișează pe Serial faza nouă, dacă s-a schimbat.
//   Eliberările respectă grila r_k = r_0 + kT: așteptare absolută (xTaskDelayUntil).
//   Durata unei faze se măsoară cu un timp monoton (millis()), nu cu epoch_ms().

void control_task(void* parameter) {
    (void)parameter;

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t periodTicks = pdMS_TO_TICKS(CONTROL_PERIOD_MS);

    while (true) {
        bool phaseChanged = false;
        Phase newPhase = CAR_GREEN;

        if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
            uint32_t elapsed = millis() - semaphoreState.phaseStartMs;

            switch (semaphoreState.currentPhase) {
                case CAR_GREEN:
                    if (semaphoreState.pedestrianRequest &&
                        elapsed >= CAR_GREEN_MIN_MS) {
                        semaphoreState.currentPhase = CAR_YELLOW;
                        phaseChanged = true;
                    }
                    break;

                case CAR_YELLOW:
                    if (elapsed >= CAR_YELLOW_MS) {
                        semaphoreState.currentPhase = ALL_RED;
                        phaseChanged = true;
                    }
                    break;

                case ALL_RED:
                    if (elapsed >= ALL_RED_MS) {
                        semaphoreState.currentPhase = PED_GREEN;
                        semaphoreState.pedestrianRequest = false;
                        phaseChanged = true;
                    }
                    break;

                case PED_GREEN:
                    if (elapsed >= PED_GREEN_MS) {
                        semaphoreState.currentPhase = CAR_GREEN;
                        phaseChanged = true;
                    }
                    break;
            }

            if (phaseChanged) {
                semaphoreState.phaseStartMs = millis();
                newPhase = semaphoreState.currentPhase;
            }

            xSemaphoreGive(stateMutex);
        }

        if (phaseChanged) {
            Serial.print("Faza noua: ");
            Serial.println(PHASE_NAMES[newPhase]);
        }

        xTaskDelayUntil(&lastWakeTime, periodTicks);
    }
}

// TODO 2: TASK PERIODIC DE IEȘIRE, cu perioada OUTPUT_PERIOD_MS.
//   La fiecare job:
//   - copiază starea (cu mutexul luat, cât mai scurt)
//   - stabilește culorile celor două lămpi din faza curentă (tabelul din README.md)
//   - calculează secundele rămase din verdele pietonilor: în PED_GREEN,
//     rotunjite în sus (7,2 s rămase -> 8); 0 în celelalte faze
//   - publică: mqtt_publish("lights", "deviceId;timestamp;faza;masini;pietoni;asteptati;secunde")
//     de exemplu "esp32-a1b2c3;1727600000000;PED_GREEN;RED;GREEN;0;8"
//   - aprinde LED-ul de pe placă (LED_PIN, aprins pe LOW) cât timp lampa
//     AȘTEPTAȚI este aprinsă
//   Așteptare absolută (xTaskDelayUntil), ca și la taskul de control.

void output_task(void* parameter) {
    (void)parameter;

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t periodTicks = pdMS_TO_TICKS(OUTPUT_PERIOD_MS);

    while (true) {
        // Copiem starea protejat de mutex.
        SemaphoreState localState;

        if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
            localState = semaphoreState;
            xSemaphoreGive(stateMutex);
        } else {
            xTaskDelayUntil(&lastWakeTime, periodTicks);
            continue;
        }

        Light cars = RED;
        Light pedestrians = RED;

        switch (localState.currentPhase) {
            case CAR_GREEN:
                cars = GREEN;
                pedestrians = RED;
                break;

            case CAR_YELLOW:
                cars = YELLOW;
                pedestrians = RED;
                break;

            case ALL_RED:
                cars = RED;
                pedestrians = RED;
                break;

            case PED_GREEN:
                cars = RED;
                pedestrians = GREEN;
                break;
        }

        // Calculăm secundele rămase pentru pietoni.
        uint32_t secondsRemaining = 0;

        if (localState.currentPhase == PED_GREEN) {
            uint32_t elapsed = millis() - localState.phaseStartMs;

            if (elapsed < PED_GREEN_MS) {
                uint32_t remainingMs = PED_GREEN_MS - elapsed;

                secondsRemaining = (remainingMs + 999) / 1000;
            }
        }

        int waitLight = localState.pedestrianRequest ? 1 : 0;

        char message[160];

        snprintf(
            message, sizeof(message), "%s;%lld;%s;%s;%s;%d;%lu",
            DEVICE_ID, (long long)epoch_ms(), PHASE_NAMES[localState.currentPhase], LIGHT_NAMES[cars], LIGHT_NAMES[pedestrians], waitLight, (unsigned long)secondsRemaining
        );

        mqtt_publish("lights", message);

        // LED-ul fizic este activ pe LOW.
        digitalWrite(LED_PIN, waitLight ? LOW : HIGH);

        // Așteptare periodică absolută.
        xTaskDelayUntil(&lastWakeTime, periodTicks);
    }
}

// TODO 3: TASK SPORADIC, tratează apăsările butonului pentru pietoni.
//   Apăsările vin din două surse, în aceeași coadă (coada butonului):
//   - butonul BOOT de pe placă, printr-o rutină de întrerupere (ISR, mai jos)
//   - butonul virtual din dashboard: comanda "BUTTON", pusă în coadă de
//     command_received()
//   Taskul:
//   - așteaptă o apăsare din coada butonului
//   - dacă de la ultima apăsare acceptată au trecut mai puțin de
//     MIN_INTERARRIVAL_MS, o respinge (mesaj pe Serial). Așa se filtrează și
//     contactele imperfecte ale butonului (bounce), care produc mai multe
//     întreruperi la o singură apăsare.
//   - altfel înregistrează cererea pietonului în starea semaforului (cu mutexul)
//     și afișează pe Serial că a acceptat-o
//   Momentul sosirii se ia când apăsarea intră în coadă (în ISR sau în
//   command_received()), nu când o preia taskul, și se pune în elementul cozii.

void sporadic_task(void* parameter) {
    (void)parameter;

    ButtonEvent event;
    uint32_t lastAcceptedMs = 0;
    bool hasAcceptedEvent = false;

    while (true) {
        if (xQueueReceive(buttonQueue, &event, portMAX_DELAY) == pdTRUE) {

            // Verificăm intervalul dintre două cereri acceptate.
            if (hasAcceptedEvent &&
                (uint32_t)(event.arrivalMs - lastAcceptedMs) < MIN_INTERARRIVAL_MS) {
                Serial.println("Cerere pieton respinsa: apasari prea apropiate.");
                continue;
            }

            if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
                semaphoreState.pedestrianRequest = true;
                xSemaphoreGive(stateMutex);

                lastAcceptedMs = event.arrivalMs;
                hasAcceptedEvent = true;

                Serial.println("Cerere pieton acceptata.");
            }
        }
    }
}

// TODO 3: RUTINA DE ÎNTRERUPERE a butonului BOOT, înregistrată în setup() cu
//   attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING).
//   Scurtă: pune o apăsare în coada butonului cu xQueueSendFromISR() și atât.
//   Fără Serial, fără mqtt_publish(), fără mutex. Se declară cu IRAM_ATTR:
//
//   void IRAM_ATTR button_isr(void) { ... }

void IRAM_ATTR button_isr() {
    ButtonEvent event;
    event.arrivalMs = millis();

    BaseType_t higherPriorityTaskWoken = pdFALSE;

    xQueueSendFromISR(buttonQueue, &event, &higherPriorityTaskWoken);

    if (higherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);
    // TODO 3: "BUTTON" merge în coada butonului (o apăsare, la fel ca cea din
    //   ISR), fără să vă blocați (timeout 0). Celelalte comenzi se ignoră:
    //   în această temă placa nu are alte comenzi.
    if (strcmp(text, "BUTTON") == 0) {
        ButtonEvent event;
        event.arrivalMs = millis();

        // Nu blocăm callback-ul MQTT dacă nu există loc în coadă.
        if (xQueueSend(buttonQueue, &event, 0) != pdTRUE) {
            Serial.println("Coada de butoane este plina!");
        }
    }
}

void setup() {
    Serial.begin(115200);
    // Fără așteptare la scriere: cu placa la USB, dar fără Serial Monitor deschis,
    // fiecare Serial.printf() ar aștepta până la 2 s să fie citit și ar întârzia
    // taskul care scrie. Așa, textul pe care nu-l citește nimeni se pierde.
    Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);  // așteptăm Serial Monitor, cel mult 3 s
    delay(200);
    Serial.println();
    Serial.printf("=== tema 03: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED stins
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    wifi_connect();
    time_sync();

    // TODO 0 (a): creați mutexul stării (xSemaphoreCreateMutex) și coada
    //   butonului (xQueueCreate). Înainte de mqtt_start(): de acolo încolo pot
    //   sosi comenzi, iar command_received() pune "BUTTON" în coadă.
    //   Notați și momentul intrării în faza inițială (CAR_GREEN).

    stateMutex = xSemaphoreCreateMutex();

    if (stateMutex == nullptr) {
        Serial.println("Eroare la crearea mutexului!");
        return;
    }

    semaphoreState.currentPhase = CAR_GREEN;
    semaphoreState.phaseStartMs = millis();
    semaphoreState.pedestrianRequest = false;

    buttonQueue = xQueueCreate(10, sizeof(ButtonEvent));

    if (buttonQueue == nullptr) {
        Serial.println("Eroare la crearea cozii!");
        return;
    }

    mqtt_start();

    // TODO 0 (b): porniți cele trei taskuri (xTaskCreate), apoi înregistrați
    //   întreruperea butonului (attachInterrupt). Alegeți prioritățile între
    //   2 și 19 (peste loopTask, sub taskurile sistemului); pentru cele două
    //   taskuri periodice, după regula Rate Monotonic. Stiva, în octeți: cel puțin 4096.

    BaseType_t outputResult = xTaskCreate(output_task, "output", 4096, nullptr, 3, nullptr);

    if (outputResult != pdPASS) {
        Serial.println("Eroare la crearea task-ului output!");
    }

    BaseType_t controlResult = xTaskCreate(control_task, "control", 4096, nullptr, 2, nullptr);

    if (controlResult != pdPASS) {
        Serial.println("Eroare la crearea taskului control!");
    }

    BaseType_t sporadicResult = xTaskCreate(sporadic_task, "sporadic", 4096, nullptr, 2, nullptr);

    if (sporadicResult != pdPASS) {
        Serial.println("Eroare la crearea taskului sporadic!");
    }

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr,FALLING);
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}

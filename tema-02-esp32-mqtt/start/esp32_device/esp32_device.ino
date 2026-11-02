// Device-ul IoT din tema 01, de data aceasta pe un ESP32-C3 Super Mini.
//
// Aceeași logică și același protocol ca device-simulator din tema 01, cu trei
// taskuri FreeRTOS în loc de trei thread-uri Java:
//
//   task periodic --(coada de alarme)--> task sporadic
//        |                                     |
//        +---- telemetry ---> MQTT <--- alarm -+
//                              |
//                          commands --> command_received() --(coada de comenzi)--> task aperiodic ---> status
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, valorile inițiale; DEVICE_ID
//                    se citește din adresa MAC a plăcii
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Taskurile le scrieți voi: TODO 0 ... TODO 3.
// Exemplele de pornire sunt în laboratorul 0 din rt-labs-esp32c3; vezi
// exemple/README.md din folderul temei.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Parametri modificabili ulterior de taskul aperiodic.
volatile uint32_t period_ms = PERIOD_MS_INITIAL;
volatile float threshold = THRESHOLD_INITIAL;

// Cozi FreeRTOS.
QueueHandle_t alarm_queue;
QueueHandle_t command_queue;

// Elementele cozilor.
struct CommandMessage {
    char text[COMMAND_MAX_LEN];
};

// Temperatura cipului, în °C, de la senzorul intern al ESP32-C3.
// Este temperatura siliciului, nu a camerei: imediat după pornirea Wi-Fi-ului
// poate fi în jur de 50 °C, apoi scade în câteva minute spre 35-40 °C.
// Rezoluția este de aproximativ 1 °C. Ca să provocați alarme, puneți pragul
// cu SET_THRESHOLD cu un grad sub valoarea citită.
float read_temperature(void) {
    return temperatureRead();
}

// TODO 1: TASK PERIODIC, cu perioada T (inițial PERIOD_MS_INITIAL).
//   Fiecare job:
//   - citește temperatura: read_temperature()
//   - publică telemetria: mqtt_publish("telemetry", "deviceId;timestamp;temperatura")
//     (timestamp = epoch_ms(), tipărit cu %lld; temperatura cu "%.2f")
//   - dacă temperatura depășește pragul (inițial THRESHOLD_INITIAL), pune
//     un eveniment în coada de alarme, fără să se blocheze (timeout 0).
//     Taskul periodic NU tratează alarma: o tratează taskul sporadic.
//   Eliberările respectă grila r_k = r_0 + kT: folosiți așteptare absolută
//   (xTaskDelayUntil), nu relativă (delay, vTaskDelay), ca să nu apară derivă.
//   Perioada și pragul se schimbă prin comenzi (taskul aperiodic), deci sunt
//   variabile globale volatile, citite la fiecare job.

void periodic_task(void* parameter) {
    (void)parameter; // tells compiler value unused, needed bc func struct

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        // Citim parametrii o singură dată pentru această iterație.
        uint32_t currentPeriod = period_ms;
        float currentThreshold = threshold;

        float temperature = read_temperature();
        int64_t timestamp = epoch_ms();

        char message[128];
        snprintf(message, sizeof(message), "%s;%lld;%.2f", 
                DEVICE_ID, (long long)timestamp, temperature);

        mqtt_publish("telemetry", message);

        // Nu așteptăm dacă nu există loc în coadă.
        if (temperature > currentThreshold) {
            if (xQueueSend(alarm_queue, &temperature, 0) != pdPASS) {
                Serial.println("[periodic] coada de alarme este plină");
            }
        }

        // Menține lansările pe o grilă temporală, nu adaugă
        // pur și simplu perioada după terminarea operațiilor.
        TickType_t periodTicks = pdMS_TO_TICKS(currentPeriod);
        if (periodTicks == 0) {
            periodTicks = 1;
        }

        xTaskDelayUntil(&lastWakeTime, periodTicks);
    }
}

// TODO 2: TASK SPORADIC, tratează alarmele de temperatură.
//   - așteaptă un eveniment din coada de alarme
//   - dacă de la ultima alarmă publicată au trecut mai puțin de
//     MIN_INTERARRIVAL_MS, respinge evenimentul (afișați un mesaj pe Serial)
//   - altfel publică: mqtt_publish("alarm", "deviceId;timestamp;temperatura;prag")
//   Pentru intervale folosiți un timp monoton, millis(), nu epoch_ms().

void sporadic_alarm_task(void* parameter) {
    (void)parameter;

    // Echivalentul lui lastRelease = 0 din Java.
    // Folosim timpul monotonic al ESP32, nu timestamp-ul Unix.
    TickType_t lastRelease = 0;
    bool hasReleased = false;

    while (true) {
        float temperature;

        // Așteaptă până când taskul periodic pune o alarmă în coadă.
        if (xQueueReceive(alarm_queue, &temperature, portMAX_DELAY) != pdPASS) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        uint32_t elapsedMs = pdTICKS_TO_MS(now - lastRelease);

        // Respingem evenimentele care sosesc prea repede.
        if (hasReleased && elapsedMs < MIN_INTERARRIVAL_MS) {
            Serial.printf(
                "[sporadic] ALARMA RESPINSA: au trecut %lu ms "
                "(interval minim %lu ms)\n",
                (unsigned long)elapsedMs,
                (unsigned long)MIN_INTERARRIVAL_MS
            );
            continue;
        }

        lastRelease = now;
        hasReleased = true;

        // Construim mesajul cu temperatura și pragul curent.
        int64_t timestamp = epoch_ms();
        float currentThreshold = threshold;

        char message[128];
        snprintf(
            message,sizeof(message),"%s;%lld;%.2f;%.2f",
            DEVICE_ID,(long long)timestamp,temperature,currentThreshold
        );

        if (mqtt_publish("alarm", message)) {
            Serial.printf("[sporadic] alarma publicata: %s\n", message);
        }
    }
}

// TODO 3: TASK APERIODIC, execută comenzile primite de la business service.
//   Comenzile ajung în command_received() (mai jos), care le pune în coada de
//   comenzi; fiecare comandă eliberează un job al taskului:
//     SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
//     SET_THRESHOLD <valoare> schimbă pragul de alarmă
//     RESET                   readuce perioada și pragul la valorile inițiale
//     STATUS                  raportează perioada și pragul curente
//   După fiecare comandă publică o confirmare:
//     mqtt_publish("status", "deviceId;timestamp;OK SET_INTERVAL 1000")
//   O comandă necunoscută sau un parametru invalid nu oprește taskul: se
//   publică "ERROR <motiv>".

static void publish_status(const char* response) {
    char message[192];

    snprintf(
        message,sizeof(message),"%s;%lld;%s",
        DEVICE_ID,(long long)epoch_ms(),response
    );

    mqtt_publish("status", message);
}

void aperiodic_command_task(void* parameter) {
    (void)parameter;

    while (true) {
        CommandMessage command = {};

        // Așteaptă până când sosește o comandă.
        if (xQueueReceive(command_queue,&command,portMAX_DELAY) != pdPASS) {
            continue;
        }

        Serial.printf("[aperiodic] comanda primita: [%s]\n",command.text);

        char* saveptr = nullptr;
        char* cmdName = strtok_r(command.text, " \t\r\n", &saveptr);
        char* arg = strtok_r(nullptr, " \t\r\n", &saveptr);
        char* extra = strtok_r(nullptr, " \t\r\n", &saveptr);

        char response[128];

        if (cmdName == nullptr) {
            snprintf(response, sizeof(response), "ERROR comanda vida");
        }
        else if (strcmp(cmdName, "SET_INTERVAL") == 0) {
            // Acceptăm un singur parametru numeric pozitiv.
            if (arg == nullptr || extra != nullptr {
                snprintf(response, sizeof(response),
                        "ERROR SET_INTERVAL necesita un argument");
            } else {
                char* endptr = nullptr;
                unsigned long value = strtoul(arg, &endptr, 10);

                if (endptr == arg || *endptr != '\0' || value == 0) {
                    snprintf(
                        response, sizeof(response),
                        "ERROR interval invalid"
                    );
                } else {
                    period_ms = (uint32_t)value;

                    snprintf(response, sizeof(response),"OK SET_INTERVAL %lu", value);
                }
            }
        }
        else if (strcmp(cmdName, "SET_THRESHOLD") == 0) {
            if (arg == nullptr || extra != nullptr) {
                snprintf(response, sizeof(response),
                        "ERROR SET_THRESHOLD necesita o valoare");
            } else {
                char* endptr = nullptr;
                float value = strtof(arg, &endptr);

                if (endptr == arg || *endptr != '\0') {
                    snprintf(response, sizeof(response),
                            "ERROR prag invalid");
                } else {
                    threshold = value;

                    snprintf(response, sizeof(response),
                            "OK SET_THRESHOLD %.2f", value);
                }
            }
        }
        else if (strcmp(cmdName, "RESET") == 0) {
            if (arg != nullptr) {
                snprintf(response, sizeof(response),
                        "ERROR RESET nu accepta parametri");
            } else {
                period_ms = PERIOD_MS_INITIAL;
                threshold = THRESHOLD_INITIAL;

                snprintf(response, sizeof(response), "OK RESET");
            }
        }
        else if (strcmp(cmdName, "STATUS") == 0) {
            if (arg != nullptr) {
                snprintf(response, sizeof(response),
                    "ERROR STATUS nu accepta parametri");
            } else {
                // Copiem valorile curente pentru răspuns.
                uint32_t currentPeriod = period_ms;
                float currentThreshold = threshold;

                snprintf(response, sizeof(response),
                        "OK STATUS periodMs=%lu threshold=%.2f",
                        (unsigned long)currentPeriod,
                        currentThreshold);
            }
        }
        else {
            snprintf(response, sizeof(response),
                    "ERROR comanda necunoscuta");
        }

        Serial.printf("[aperiodic] raspuns: %s\n", response);
        publish_status(response);
    }
}

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    // TODO 3: puneți comanda în coada de comenzi, fără să vă blocați
    //   (timeout 0). text este valid doar în timpul acestui apel: elementul
    //   cozii trebuie să conțină textul însuși (de exemplu un struct cu
    //   char text[COMMAND_MAX_LEN]), nu un pointer.
    Serial.printf("[mqtt] Comanda primita: [%s]\n", text);

    CommandMessage command = {};
    snprintf(command.text, sizeof(command.text), "%s", text);

    if (xQueueSend(command_queue, &command, 0) != pdPASS) {
        Serial.println("[mqtt] EROARE: coada de comenzi este plina");
    } else {
        Serial.println("[mqtt] Comanda adaugata in coada");
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
    Serial.printf("=== tema 02: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    wifi_connect();
    time_sync();

    // TODO 0 (a): creați cele două cozi (xQueueCreate): coada de alarme și
    //   coada de comenzi. Înainte de mqtt_start(): de acolo încolo pot sosi
    //   comenzi, iar command_received() le pune în coada de comenzi.

    alarm_queue = xQueueCreate(10, sizeof(float));
    command_queue = xQueueCreate(10, sizeof(CommandMessage));

    if (alarm_queue == nullptr || command_queue == nullptr) {
        Serial.println("[setup] eroare la crearea cozilor");
        return;
    }

    mqtt_start();

    // TODO 0 (b): porniți cele trei taskuri (xTaskCreate). După mqtt_start(),
    //   ca primele mesaje publicate să nu se piardă. Alegeți prioritățile
    //   între 2 și 19: peste loopTask (1), sub taskurile sistemului pentru
    //   evenimente și Wi-Fi (20, 23). Stiva, în octeți: cel puțin 4096.

    BaseType_t result = xTaskCreate(periodic_task,"periodic",4096,nullptr,3,nullptr);

    if (result != pdPASS) {
        Serial.println("[setup] eroare la pornirea taskului periodic");
    }

    BaseType_t sporadicResult = xTaskCreate(sporadic_alarm_task,"sporadic",4096,nullptr,3,nullptr);

    if (sporadicResult != pdPASS) {
        Serial.println("[setup] eroare la pornirea taskului sporadic");
    }
    
    BaseType_t aperiodicResult = xTaskCreate(aperiodic_command_task,"aperiodic",4096,nullptr,3,nullptr);

    if (aperiodicResult != pdPASS) {
        Serial.println("[setup] eroare la pornirea taskului aperiodic");
    }
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}

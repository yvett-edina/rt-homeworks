package ssatr.service;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * Punctul de pornire al serviciului business.
 *
 * Rulare:  mvn -q compile exec:java
 *
 * Serviciul:
 *  - primește telemetria, alarmele și confirmările de la toate device-urile;
 *  - le stochează în fișiere CSV (CsvStorage);
 *  - păstrează ultima stare a fiecărui device (Dashboard);
 *  - citește comenzi de la tastatură și le trimite device-urilor (CommandConsole).
 */
public class BusinessService {

    public static void main(String[] args) throws Exception {
        MqttConnection mqtt = new MqttConnection(Config.STUDENT + "-business-service");
        mqtt.connect();

        BlockingQueue<String> telemetryInbox = new LinkedBlockingQueue<>();
        BlockingQueue<String> alarmInbox = new LinkedBlockingQueue<>();
        BlockingQueue<String> statusInbox = new LinkedBlockingQueue<>();

        // "+" înseamnă "orice device": primim mesajele de la toate device-urile
        mqtt.subscribe(Config.telemetryTopic("+"), telemetryInbox);
        mqtt.subscribe(Config.alarmTopic("+"), alarmInbox);
        mqtt.subscribe(Config.statusTopic("+"), statusInbox);

        CsvStorage storage = new CsvStorage("data");
        Dashboard dashboard = new Dashboard();

        new Thread(new MessageProcessor("telemetry", telemetryInbox, storage, dashboard), "telemetry").start();
        new Thread(new MessageProcessor("alarm", alarmInbox, storage, dashboard), "alarm").start();
        new Thread(new MessageProcessor("status", statusInbox, storage, dashboard), "status").start();

        // consola rulează pe thread-ul principal
        new CommandConsole(mqtt, dashboard).run();

        mqtt.disconnect();
        System.exit(0);
    }
}

package ssatr.service;

import java.util.concurrent.BlockingQueue;

/**
 * Preia mesajele de un anumit tip ("telemetry", "alarm" sau "status") din coada
 * inbox, le salvează și actualizează dashboard-ul.
 *
 * Formatul mesajelor (câmpuri separate prin ";"):
 *   telemetry:  deviceId;timestamp;temperatura
 *   alarm:      deviceId;timestamp;temperatura;prag
 *   status:     deviceId;timestamp;text
 */
public class MessageProcessor implements Runnable {

    private final String kind;
    private final BlockingQueue<String> inbox;
    private final CsvStorage storage;
    private final Dashboard dashboard;

    public MessageProcessor(String kind, BlockingQueue<String> inbox, CsvStorage storage, Dashboard dashboard) {
        this.kind = kind;
        this.inbox = inbox;
        this.storage = storage;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        // TODO 4: într-o buclă:
        //   - preluați un mesaj: inbox.take()
        //   - salvați mesajul în fișierul CSV: storage.append(kind, mesaj)
        //     (CsvStorage este gata implementată)
        //   - actualizați dashboard-ul, în funcție de kind
        //   - alarmele și confirmările se afișează imediat în consolă
        //   - un mesaj greșit (format invalid) nu trebuie să oprească thread-ul

        while (!Thread.currentThread().isInterrupted()) {
            String msg;

            try {
                msg = inbox.take();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }

            try {
                String[] parts = msg.split(";");
                String deviceId = parts[0];
                long timestamp = Long.parseLong(parts[1]);

                switch (kind) {
                    case "telemetry" -> {
                        double temperature = Double.parseDouble(parts[2]);
                        dashboard.updateTelemetry(deviceId, timestamp, temperature);
                    }
                    case "alarm" -> {
                        double temperature = Double.parseDouble(parts[2]);
                        double threshold = Double.parseDouble(parts[3]);
                        dashboard.updateAlarm(deviceId, timestamp, temperature, threshold);
                        System.out.printf("   [dashboard] ALARMA: %s la %d: temperatura %.2f depaseste pragul %.2f%n",
                                deviceId, timestamp, temperature, threshold);
                    }
                    case "status" -> {
                        String text = parts[2];
                        dashboard.updateStatus(deviceId, timestamp, text);
                        System.out.printf("   [dashboard] STATUS: %s la %d: %s%n", deviceId, timestamp, text);
                    }
                    default -> System.err.printf("   [dashboard] Mesaj necunoscut de tip '%s': %s%n", kind, msg);
                }

                storage.append(kind, msg);
            } catch (Exception e) {
                System.err.printf("   [dashboard] Eroare procesare mesaj '%s': %s%n", msg, e.getMessage());
            }
        }
    }
}

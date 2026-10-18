package ssatr.service;

import java.util.HashMap;
import java.util.Map;

/**
 * Ultima stare cunoscută a fiecărui device: ultima temperatură, momentul
 * ultimului mesaj, numărul de alarme etc.
 *
 * Este actualizată de thread-urile MessageProcessor și citită de CommandConsole,
 * deci accesul trebuie sincronizat.
 */
public class Dashboard {

    // TODO 5: alegeți structura de date pentru starea device-urilor
    //   (de exemplu un Map<String, ...> cu cheia deviceId)
    //   și adăugați metodele de actualizare apelate din MessageProcessor.

    private final Map<String, DeviceState> devices = new HashMap<>();

    private static class DeviceState {
        double lastTemperature;
        long lastTimestamp;
        int alarmCount;

        double threshold;
        String status = "-";
        boolean hasTemperature;
    }

    public synchronized void updateTelemetry(String deviceId, long timestamp, double temperature) {

        DeviceState state = devices.get(deviceId);

        if (state == null) {
            state = new DeviceState();
            devices.put(deviceId, state);
        }

        state.lastTemperature = temperature;
        state.lastTimestamp = timestamp;
        state.hasTemperature = true;
    }

    public synchronized void updateAlarm(
            String deviceId, long timestamp,
            double temperature, double threshold) {

        DeviceState state = devices.get(deviceId);

        if (state == null) {
            state = new DeviceState();
            devices.put(deviceId, state);
        }

        state.lastTemperature = temperature;
        state.threshold = threshold;
        state.lastTimestamp = timestamp;
        state.alarmCount++;
        state.hasTemperature = true;
    }

    public synchronized void updateStatus(
            String deviceId, long timestamp, String text) {

        DeviceState state = devices.get(deviceId);

        if (state == null) {
            state = new DeviceState();
            devices.put(deviceId, state);
        }

        state.lastTimestamp = timestamp;
        state.status = text;
    }

    /** Afișează un tabel cu starea tuturor device-urilor. */
    public synchronized void print() {
        // TODO 5: afișați câte un rând pentru fiecare device, de exemplu:
        //   DEVICE     TEMP    ULTIMUL MESAJ   ALARME
        //   device-1   23.41   acum 1 s        2

        System.out.printf("%-10s %-8s %-20s %-8s %-10s%n", "DEVICE", "TEMP", "ULTIMUL MESAJ", "ALARME", "STATUS");

        for (Map.Entry<String, DeviceState> entry : devices.entrySet()) {
            String deviceId = entry.getKey();
            DeviceState state = entry.getValue();

            String temperature = state.hasTemperature
                    ? String.format(java.util.Locale.US, "%.2f",
                    state.lastTemperature) : "-";

            long secondsAgo = (System.currentTimeMillis() - state.lastTimestamp) / 1000;

            System.out.printf("%-10s %-8.2f %-20s %-8d %-10s%n",
                    deviceId,
                    state.lastTemperature,
                    secondsAgo,
                    state.alarmCount,
                    state.status);
        }

        if (devices.isEmpty()) {
            System.out.println("Nu exista device-uri inregistrate.");
        }
    }
}

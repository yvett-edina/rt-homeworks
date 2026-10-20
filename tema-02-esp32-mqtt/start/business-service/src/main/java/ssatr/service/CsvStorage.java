package ssatr.service;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * Stocarea mesajelor în fișiere CSV (clasă gata implementată).
 *
 * Fiecare tip de mesaj are fișierul lui:
 *   data/telemetry.csv   deviceId;timestamp;temperatura
 *   data/alarm.csv       deviceId;timestamp;temperatura;prag
 *   data/status.csv      deviceId;timestamp;text
 *
 * Separatorul este ";", la fel ca în mesajele MQTT, deci mesajul se scrie
 * exact așa cum a sosit.
 */
public class CsvStorage {

    private final Path directory;

    public CsvStorage(String directory) {
        this.directory = Path.of(directory);
    }

    /**
     * Adaugă un mesaj la sfârșitul fișierului kind.csv.
     *
     * @param kind    "telemetry", "alarm" sau "status"
     * @param message mesajul primit pe MQTT, nemodificat
     *
     * Metoda este apelată din mai multe thread-uri (câte un MessageProcessor
     * pentru fiecare tip de mesaj). Este synchronized, ca două thread-uri să nu
     * scrie în același timp și să nu se amestece liniile.
     */
    public synchronized void append(String kind, String message) {
        Path file = directory.resolve(kind + ".csv");
        try {
            Files.createDirectories(directory);

            // un fișier nou începe cu rândul de antet
            if (Files.notExists(file)) {
                Files.writeString(file, header(kind) + "\n");
            }

            Files.writeString(file, message + "\n", StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (IOException e) {
            System.out.println("Eroare la scrierea în " + file + ": " + e.getMessage());
        }
    }

    private static String header(String kind) {
        switch (kind) {
            case "telemetry":
                return "deviceId;timestamp;temperatura";
            case "alarm":
                return "deviceId;timestamp;temperatura;prag";
            case "status":
                return "deviceId;timestamp;text";
            default:
                return "mesaj";
        }
    }
}

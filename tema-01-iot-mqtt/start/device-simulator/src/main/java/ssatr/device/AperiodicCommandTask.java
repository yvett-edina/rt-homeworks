package ssatr.device;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * TASK APERIODIC: execută comenzile primite de la business service.
 *
 * Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
 * puse în coada commands. Fiecare comandă eliberează un job al taskului.
 * Comenzi care trebuie suportate:
 *
 *   SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
 *   SET_THRESHOLD <valoare> schimbă pragul de alarmă
 *   RESET                   readuce senzorul la valoarea inițială
 *   STATUS                  raportează perioada și pragul curente
 *
 * După executarea fiecărei comenzi, device-ul publică pe Config.statusTopic(deviceId)
 * o confirmare: "deviceId;timestamp;text", de exemplu
 * "device-1;1727600000000;OK SET_INTERVAL 1000".
 */
public class AperiodicCommandTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<String> commands;
    private final PeriodicTelemetryTask periodic;

    public AperiodicCommandTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                BlockingQueue<String> commands, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.commands = commands;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        // TODO 3: implementați taskul aperiodic:
        //   - preluați comenzile din coadă: commands.take()
        //   - separați numele comenzii de parametru: command.split(" ")
        //   - executați comanda (periodic.setPeriodMs(...), sensor.reset() etc.)
        //   - publicați confirmarea pe Config.statusTopic(deviceId)
        //   - o comandă necunoscută sau un parametru invalid nu trebuie să
        //     oprească taskul: publicați "ERROR <motiv>"
        //
        // Vezi exemplul exemple/01-taskuri/.../AperiodicTaskExample.java

        while(!Thread.currentThread().isInterrupted()) {
            String cmd;

            try {
                cmd = commands.take();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }

            String response;
            String cmdName = cmd.split(" ")[0];
            System.out.println("Comanda primită: [" + cmd + "]");

            switch (cmdName) {
                case "SET_INTERVAL":
                    long period = Long.parseLong(cmd.split(" ")[1]);
                    periodic.setPeriodMs(period);
                    response = "OK SET_INTERVAL " + period;
                    break;

                case "SET_THRESHOLD":
                    double threshold = Double.parseDouble(cmd.split(" ")[1]);
                    periodic.setThreshold(threshold);
                    response = "OK SET_THRESHOLD " + threshold;
                    break;

                case "RESET":
                    sensor.reset();
                    response = "OK RESET";
                    break;

                case "STATUS":
                    response = "OK STATUS periodMs=" + periodic.getPeriodMs() + " threshold=" + periodic.getThreshold();
                    break;

                default:
                    response ="ERROR: Unknown command";
                    break;
            }

            String message = deviceId + ";" + System.currentTimeMillis() + ";" + response;
            mqtt.publish(Config.statusTopic(deviceId), message);
        }
    }
}

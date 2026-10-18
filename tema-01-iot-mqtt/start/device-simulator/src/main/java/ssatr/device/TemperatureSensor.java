package ssatr.device;

import java.util.Random;

/**
 * Senzor de temperatură simulat (clasă gata implementată).
 *
 * Valoarea se modifică puțin la fiecare citire. Din când în când apare un
 * vârf de temperatură, ca să se poată declanșa alarme.
 */
public class TemperatureSensor {

    private static final double INITIAL = 22.0;

    private final Random random = new Random();
    private double value = INITIAL;

    public synchronized double read() {
        value += (random.nextDouble() - 0.5);      // variație mică: ±0.5 °C
        if (random.nextInt(10) == 0) {
            value += 3 + random.nextDouble() * 5;   // vârf ocazional
        }
        if (value > INITIAL + 2) {
            value -= 1;                             // revine treptat spre valoarea normală
        }
        return value;
    }

    public synchronized void reset() {
        value = INITIAL;
        System.out.println("Senzor resetat la " + INITIAL + " °C");
    }
}

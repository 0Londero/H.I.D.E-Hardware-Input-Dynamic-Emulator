/**
 * H.I.D.E - Teste do mouse
 * ========================
 * Sketch de diagnostico: confirma se o mouse e reconhecido pelo PIO-USB,
 * se aceita o "boot protocol" e mostra os relatorios recebidos.
 *
 * Como usar:
 *   1. Configure Tools > USB Stack = "Adafruit TinyUSB" e CPU Speed = 240 MHz.
 *   2. Abra o Monitor Serial (115200 baud).
 *   3. Conecte o mouse (com fio ou via dongle 2.4 GHz) no conector USB-A.
 *   4. Interpretacao:
 *      - "mouse montado" + "BOOT PROTOCOL OK" + bytes mudando ao mexer
 *        => mouse OK. Pode gravar o firmware principal (firmware.ino).
 *      - "ainda em REPORT protocol" => o firmware principal precisa ler
 *        "report protocol" (adaptar o parser HID).
 *      - nada aparece => problema de solda, clock ou cabo.
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>
#include <hardware/watchdog.h>

constexpr uint8_t HOST_USB_DP_PIN = 0; // GP0 = D+, GP1 = D- (pino ao lado)

Adafruit_USBH_Host USBHost;

// ------------------------------------------------------------------
// USB Host: callbacks HID (apenas diagnostico, sem pass-through)
// ------------------------------------------------------------------
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const*, uint16_t desc_len) {
    Serial.println("== mouse montado ==");
    Serial.print("endereco="); Serial.println(dev_addr);
    Serial.print("instancia="); Serial.println(instance);

    const uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    Serial.print("interface=");
    Serial.println(proto == HID_ITF_PROTOCOL_MOUSE ? "MOUSE"
                 : proto == HID_ITF_PROTOCOL_KEYBOARD ? "KEYBOARD" : "OUTRO");

    Serial.print("protocolo_atual=");
    Serial.println(tuh_hid_get_protocol(dev_addr, instance) == HID_PROTOCOL_BOOT
                   ? "BOOT" : "REPORT");
    Serial.print("desc_len="); Serial.println(desc_len);

    if (proto != HID_ITF_PROTOCOL_MOUSE) {
        Serial.println("ATENCAO: dispositivo nao e mouse; ignorando.");
        return;
    }

    if (tuh_hid_get_protocol(dev_addr, instance) == HID_PROTOCOL_BOOT) {
        Serial.println("ja esta em boot protocol; comecando a receber.");
        tuh_hid_receive_report(dev_addr, instance);
        return;
    }

    Serial.println("pedindo boot protocol...");
    if (!tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT)) {
        Serial.println("FALHA: set_protocol retornou false.");
        tuh_hid_receive_report(dev_addr, instance);
    }
}

void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t p) {
    Serial.print("set_protocol completo: ");
    if (p == HID_PROTOCOL_BOOT) {
        Serial.println("BOOT PROTOCOL OK.");
        tuh_hid_receive_report(dev_addr, instance);
    } else {
        Serial.println("ainda em REPORT protocol (boot NAO aceito).");
    }
}

void tuh_hid_umount_cb(uint8_t, uint8_t) {
    Serial.println("== mouse desconectado ==");
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    const bool boot =
        tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_MOUSE &&
        tuh_hid_get_protocol(dev_addr, instance) == HID_PROTOCOL_BOOT;

    Serial.print("relatorio len="); Serial.print(len);
    Serial.print(" boot="); Serial.print(boot ? "sim" : "nao");
    Serial.print(" bytes=");
    if (report != nullptr) {
        for (uint16_t i = 0; i < len; i++) {
            if (report[i] < 16) Serial.print('0');
            Serial.print(report[i], HEX);
            Serial.print(' ');
        }
    } else {
        Serial.print("(null)");
    }
    Serial.println();

    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_MOUSE) {
        tuh_hid_receive_report(dev_addr, instance);
    }
}

// ------------------------------------------------------------------
// Core 0: USB device (somente a serial CDC para os logs)
// ------------------------------------------------------------------
void setup() {
    // Clock: configure 240 MHz no menu do board (PIO-USB exige multiplo de 12 MHz).
    watchdog_enable(1000, 1);
    if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
    Serial.begin(115200);
    delay(100);
    Serial.println("H.I.D.E - teste do mouse");
    Serial.println("Plugue o mouse no conector USB-A e observe o log.");
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
    TinyUSBDevice.task();
#endif
    watchdog_update();
}

// ------------------------------------------------------------------
// Core 1: USB host via PIO-USB
// ------------------------------------------------------------------
void setup1() {
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = HOST_USB_DP_PIN;
    USBHost.configure_pio_usb(1, &cfg);
    USBHost.begin(1);
}

void loop1() {
    USBHost.task();
    watchdog_update();
}

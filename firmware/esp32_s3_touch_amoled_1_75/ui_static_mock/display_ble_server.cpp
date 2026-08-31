#include "display_ble_server.h"

#include "debug_log_config.h"
#include "display_product_page.h"
#include "display_protocol_v2.h"
#include "display_state.h"

#if defined(__has_include)
#if __has_include(<NimBLEDevice.h>)
#define DISPLAY_BLE_NIMBLE_AVAILABLE 1
#include <NimBLEDevice.h>
#else
#define DISPLAY_BLE_NIMBLE_AVAILABLE 0
#endif
#else
#define DISPLAY_BLE_NIMBLE_AVAILABLE 0
#endif

namespace {

// Keep legacy advertising small: flags + short name only. The full 128-bit
// service UUID stays in the GATT server because putting it in advertising data
// with a long name can exceed 31 bytes and make phones miss the peripheral.
constexpr char kDisplayBleAdvertisedName[] = "IG_ROUND";
constexpr char kDisplayBleServiceUuid[] = "ADB402C0-B1C6-11ED-AFA1-0242AC120010";
constexpr char kDisplayBleStateCharacteristicUuid[] = "ADB40201-B1C6-11ED-AFA1-0242AC120011";
constexpr uint32_t kDisplayBleNotifyIntervalMs = 500;
DisplayProductPage displayBleLastLoggedPage = DisplayProductPage::Other;
uint16_t displayBleLastLoggedWireCalories = UINT16_MAX;

#if DISPLAY_BLE_NIMBLE_AVAILABLE
NimBLEServer* displayBleServer = nullptr;
NimBLECharacteristic* displayBleStateCharacteristic = nullptr;
volatile bool displayBleAdvertising = false;
uint16_t displayBleLastRejectedMtu = 0;

bool setCurrentDisplayStateValue(NimBLECharacteristic* characteristic);

class DisplayBleServerCallbacks : public NimBLEServerCallbacks {
  public:
    void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override
    {
        (void)connInfo;
#if ENABLE_DISPLAY_BLE_LOG
        Serial.printf("DISPLAY_BLE_MTU: negotiated=%u\n", static_cast<unsigned>(mtu));
#endif
    }
};

class DisplayStateCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onRead(
        NimBLECharacteristic* characteristic,
        NimBLEConnInfo& connInfo) override
    {
        (void)connInfo;
        setCurrentDisplayStateValue(characteristic);
    }
};

DisplayBleServerCallbacks displayBleServerCallbacks;
DisplayStateCharacteristicCallbacks displayStateCharacteristicCallbacks;
#endif

volatile bool displayBleReady = false;
volatile bool displayBleConnected = false;
uint32_t displayBleLastNotifyAttemptMs = 0;

void printBeginFailure(const char* reason)
{
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE_ERR:");
    Serial.print("reason=");
    Serial.println(reason);
#else
    (void)reason;
#endif
}

const char* productPageName(DisplayProductPage page)
{
    switch (page) {
        case DisplayProductPage::WorkoutGps: return "workout";
        case DisplayProductPage::IgControl: return "ig_control";
        case DisplayProductPage::Other:
        default: return "other";
    }
}

void printCaloriesPacketSummary(const uint8_t* packet, size_t packetLength)
{
#if ENABLE_DISPLAY_BLE_LOG
    if (packet == nullptr || packetLength != PROTOCOL_V2_PACKET_LENGTH) {
        return;
    }

    const DisplayProductPage page = displayProductPageGet();
    const uint16_t transmittedCalories =
        static_cast<uint16_t>(packet[23]) |
        (static_cast<uint16_t>(packet[24]) << 8);
    if (page == displayBleLastLoggedPage &&
        transmittedCalories == displayBleLastLoggedWireCalories) {
        return;
    }

    displayBleLastLoggedPage = page;
    displayBleLastLoggedWireCalories = transmittedCalories;
    Serial.printf(
        "DISPLAY_BLE_CALORIES: page=%s packet_len=%u payload_len=0x%02X cmd06=%u\n",
        productPageName(page),
        static_cast<unsigned>(packetLength),
        static_cast<unsigned>(packet[1]),
        static_cast<unsigned>(transmittedCalories)
    );
#else
    (void)packet;
    (void)packetLength;
#endif
}

#if DISPLAY_BLE_NIMBLE_AVAILABLE
bool packDisplayStatePacket(uint8_t* packet, size_t packetSize, size_t* packetLength)
{
    if (displayStatePackProtocolV2Packet(packet, packetSize, packetLength) &&
        packetLength != nullptr && *packetLength == PROTOCOL_V2_PACKET_LENGTH) {
        return true;
    }

#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE_ERR: pack failed");
#endif
    return false;
}

bool setCurrentDisplayStateValue(NimBLECharacteristic* characteristic)
{
    if (characteristic == nullptr) {
        return false;
    }

    uint8_t packet[PROTOCOL_V2_PACKET_LENGTH] = {};
    size_t packetLength = 0;
    if (!packDisplayStatePacket(packet, sizeof(packet), &packetLength)) {
        return false;
    }

    characteristic->setValue(packet, packetLength);
    return true;
}

bool connectedPeersSupportProtocolV2()
{
    if (displayBleServer == nullptr) {
        return false;
    }

    const size_t connectedCount = displayBleServer->getConnectedCount();
    for (size_t i = 0; i < connectedCount; ++i) {
        const NimBLEConnInfo connInfo =
            displayBleServer->getPeerInfo(static_cast<uint8_t>(i));
        const uint16_t mtu = connInfo.getMTU();
        if (mtu < PROTOCOL_V2_MIN_MTU) {
#if ENABLE_DISPLAY_BLE_LOG
            if (displayBleLastRejectedMtu != mtu) {
                Serial.printf(
                    "BLE_PROTOCOL_V2_ERR: negotiated_mtu=%u required>=%u\n",
                    static_cast<unsigned>(mtu),
                    static_cast<unsigned>(PROTOCOL_V2_MIN_MTU)
                );
            }
#endif
            displayBleLastRejectedMtu = mtu;
            return false;
        }
    }

    displayBleLastRejectedMtu = 0;
    return connectedCount > 0;
}

bool startDisplayBleAdvertising()
{
    if (displayBleAdvertising) {
        return true;
    }

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (advertising == nullptr) {
        printBeginFailure("advertising_failed");
        return false;
    }

    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setName(kDisplayBleAdvertisedName);
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: adv mode = name only");
    Serial.println("DISPLAY_BLE: adv short name = IG_ROUND");
#endif
    advertising->setAdvertisementData(advData);
    advertising->setMinInterval(160);
    advertising->setMaxInterval(320);
    if (!advertising->start()) {
        printBeginFailure("advertising_failed");
        return false;
    }
    displayBleAdvertising = true;
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: advertising started");
    Serial.println("DISPLAY_BLE: advertising as IG_ROUND");
#endif
    return true;
}

bool notifyDisplayStatePacket()
{
    if (!displayBleReady || displayBleStateCharacteristic == nullptr || !displayBleHasSubscriber()) {
        return false;
    }

    if (!connectedPeersSupportProtocolV2()) {
        return false;
    }

    uint8_t packet[PROTOCOL_V2_PACKET_LENGTH] = {};
    size_t packetLength = 0;
    if (!packDisplayStatePacket(packet, sizeof(packet), &packetLength)) {
        return false;
    }

    displayBleStateCharacteristic->setValue(packet, packetLength);
    if (!displayBleStateCharacteristic->notify()) {
#if ENABLE_DISPLAY_BLE_LOG
        Serial.println("DISPLAY_BLE_ERR: notify failed");
#endif
        return false;
    }
    printCaloriesPacketSummary(packet, packetLength);
    return true;
}

bool tryNotifyDisplayStatePacket(bool force)
{
    const uint32_t now = millis();
    if (!force &&
        (now - displayBleLastNotifyAttemptMs) < kDisplayBleNotifyIntervalMs) {
        return false;
    }

    // Limit attempts as well as successful sends so an unavailable MTU or
    // subscriber cannot turn the main loop into a busy retry path.
    displayBleLastNotifyAttemptMs = now;
    return notifyDisplayStatePacket();
}

void updateDisplayBleConnectionState()
{
    const bool connectedNow = displayBleServer != nullptr && displayBleServer->getConnectedCount() > 0;
    if (connectedNow == displayBleConnected) {
        return;
    }

    displayBleConnected = connectedNow;
    displayBleAdvertising = false;
    displayBleLastRejectedMtu = 0;

    if (connectedNow) {
#if ENABLE_DISPLAY_BLE_LOG
        Serial.println("DISPLAY_BLE: client connected");
#endif
        tryNotifyDisplayStatePacket(true);
    } else {
#if ENABLE_DISPLAY_BLE_LOG
        Serial.println("DISPLAY_BLE: client disconnected");
#endif
        startDisplayBleAdvertising();
    }
}
#endif

} // namespace

bool displayBleBegin()
{
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: begin called");
#endif

    if (displayBleReady) {
        return true;
    }

#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: begin");
    Serial.println("DISPLAY_BLE: service uuid = ADB402C0-B1C6-11ED-AFA1-0242AC120010");
    Serial.println("DISPLAY_BLE: state char uuid = ADB40201-B1C6-11ED-AFA1-0242AC120011");
    Serial.println("BLE_PROTOCOL_V2: packet_len=34 preferred_mtu=64 min_mtu=37");
    Serial.println("BLE_NOTIFY_RATE: interval_ms=500 rate_hz=2");
#endif

#if !DISPLAY_BLE_NIMBLE_AVAILABLE
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE_ERR: NimBLE backend not available, BLE server disabled");
    Serial.println("DISPLAY_BLE: install NimBLE-Arduino to enable low-memory BLE notify.");
#endif
    printBeginFailure("nimble_backend_not_available");
    return false;
#else
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: NimBLE backend enabled");
#endif
    if (!NimBLEDevice::init(kDisplayBleAdvertisedName)) {
        printBeginFailure("nimble_init_failed");
        return false;
    }
    NimBLEDevice::setMTU(PROTOCOL_V2_PREFERRED_MTU);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: NimBLEDevice init called");
#endif

    displayBleServer = NimBLEDevice::createServer();
    if (displayBleServer == nullptr) {
        printBeginFailure("create_server_failed");
        return false;
    }
    displayBleServer->setCallbacks(&displayBleServerCallbacks, false);

    NimBLEService* service = displayBleServer->createService(kDisplayBleServiceUuid);
    if (service == nullptr) {
        printBeginFailure("create_service_failed");
        return false;
    }
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: service created");
#endif

    displayBleStateCharacteristic = service->createCharacteristic(
        kDisplayBleStateCharacteristicUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    if (displayBleStateCharacteristic == nullptr) {
        printBeginFailure("create_characteristic_failed");
        return false;
    }
    displayBleStateCharacteristic->setCallbacks(
        &displayStateCharacteristicCallbacks
    );
#if ENABLE_DISPLAY_BLE_LOG
    Serial.println("DISPLAY_BLE: state characteristic created");
#endif

    if (!setCurrentDisplayStateValue(displayBleStateCharacteristic)) {
        printBeginFailure("initial_packet_failed");
        return false;
    }

    if (!displayBleServer->start()) {
        printBeginFailure("server_start_failed");
        return false;
    }

    if (!startDisplayBleAdvertising()) {
        return false;
    }

    displayBleReady = true;
    displayBleLastNotifyAttemptMs = millis();
    return true;
#endif
}

void displayBleLoop()
{
#if DISPLAY_BLE_NIMBLE_AVAILABLE
    if (!displayBleReady) {
        return;
    }

    updateDisplayBleConnectionState();

    if (!displayBleConnected) {
        startDisplayBleAdvertising();
    }

    tryNotifyDisplayStatePacket(false);
#endif
}

bool displayBleIsConnected()
{
    return displayBleConnected;
}

bool displayBleHasSubscriber()
{
    // Portable NimBLE CCCD callbacks vary across NimBLE-Arduino versions.
    // For this first low-memory server pass, treat a connected client as notify-ready;
    // NimBLE itself only delivers notifications to clients that enabled CCCD.
    return displayBleConnected;
}

void displayBleNotifyTest()
{
#if DISPLAY_BLE_NIMBLE_AVAILABLE
    tryNotifyDisplayStatePacket(false);
#endif
}

void displayBleNotifyStatus(
    bool projectionOn,
    bool highBrightness,
    uint8_t brightnessPercent,
    uint16_t displayMask,
    uint8_t displayBatteryPercent,
    uint32_t workoutDurationSeconds,
    float speedKmh,
    uint16_t caloriesKcal,
    float distanceMeters,
    uint16_t lapCount
)
{
    (void)projectionOn;
    (void)highBrightness;
    (void)brightnessPercent;
    (void)displayMask;
    (void)displayBatteryPercent;
    (void)workoutDurationSeconds;
    (void)speedKmh;
    (void)caloriesKcal;
    (void)distanceMeters;
    (void)lapCount;

#if DISPLAY_BLE_NIMBLE_AVAILABLE
    // Display State is the single internal state source. Touch UI and Voice
    // Control modify it, GPS and Motion Logic update data fields, and BLE only
    // packs and publishes the current snapshot to external clients. Keep this
    // public API for compatibility, but notify the current Display State.
    tryNotifyDisplayStatePacket(false);
#endif
}

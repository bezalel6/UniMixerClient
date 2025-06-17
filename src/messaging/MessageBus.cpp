#include "MessageBus.h"
#include "Messages.h"
#include <esp_log.h>

static const char* TAG = "MessageBus";

namespace Messaging {

// Static member definitions
Transport* MessageBus::primaryTransport = nullptr;
Transport* MessageBus::secondaryTransport = nullptr;
bool MessageBus::dualTransportMode = false;
unsigned long MessageBus::lastActivity = 0;
bool MessageBus::initialized = false;

bool MessageBus::Init() {
    if (initialized) {
        ESP_LOGW(TAG, "MessageBus already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing MessageBus");

    lastActivity = millis();
    initialized = true;

    ESP_LOGI(TAG, "MessageBus initialized successfully");
    return true;
}

void MessageBus::Deinit() {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing MessageBus");

    if (primaryTransport && primaryTransport->Deinit) {
        primaryTransport->Deinit();
    }

    if (secondaryTransport && secondaryTransport->Deinit) {
        secondaryTransport->Deinit();
    }

    primaryTransport = nullptr;
    secondaryTransport = nullptr;
    dualTransportMode = false;
    initialized = false;
}

void MessageBus::Update() {
    if (!initialized) {
        return;
    }

    if (primaryTransport && primaryTransport->Update) {
        primaryTransport->Update();
    }

    if (secondaryTransport && secondaryTransport->Update) {
        secondaryTransport->Update();
    }
}

bool MessageBus::Publish(const char* topic, const char* payload) {
    if (!initialized || !topic || !payload) {
        ESP_LOGE(TAG, "Cannot publish: invalid parameters or not initialized");
        return false;
    }

    bool primaryResult = false;
    bool secondaryResult = false;

    // Publish to primary transport
    if (primaryTransport) {
        primaryResult = PublishToTransport(primaryTransport, topic, payload);
    }

    // Publish to secondary transport if in dual mode
    if (dualTransportMode && secondaryTransport) {
        secondaryResult = PublishToTransport(secondaryTransport, topic, payload);
    }

    // Update activity if any transport succeeded
    if (primaryResult || secondaryResult) {
        UpdateActivity();
        return true;
    }

    return false;
}

bool MessageBus::PublishDelayed(const char* topic, const char* payload) {
    if (!initialized || !topic || !payload) {
        ESP_LOGE(TAG, "Cannot publish delayed: invalid parameters or not initialized");
        return false;
    }

    bool primaryResult = false;
    bool secondaryResult = false;

    // Publish delayed to primary transport
    if (primaryTransport && primaryTransport->PublishDelayed) {
        primaryResult = primaryTransport->PublishDelayed(topic, payload);
    }

    // Publish delayed to secondary transport if in dual mode
    if (dualTransportMode && secondaryTransport && secondaryTransport->PublishDelayed) {
        secondaryResult = secondaryTransport->PublishDelayed(topic, payload);
    }

    return primaryResult || secondaryResult;
}

bool MessageBus::IsConnected() {
    if (!initialized) {
        return false;
    }

    bool primaryConnected = primaryTransport && primaryTransport->IsConnected && primaryTransport->IsConnected();

    if (dualTransportMode && secondaryTransport) {
        bool secondaryConnected = secondaryTransport->IsConnected && secondaryTransport->IsConnected();
        return primaryConnected || secondaryConnected;  // At least one connected
    }

    return primaryConnected;
}

ConnectionStatus MessageBus::GetStatus() {
    if (!initialized || !primaryTransport || !primaryTransport->GetStatus) {
        return ConnectionStatus::Disconnected;
    }

    return primaryTransport->GetStatus();
}

const char* MessageBus::GetStatusString() {
    if (!initialized || !primaryTransport || !primaryTransport->GetStatusString) {
        return "Not Initialized";
    }

    return primaryTransport->GetStatusString();
}

bool MessageBus::RegisterHandler(const Handler& handler) {
    if (!initialized) {
        ESP_LOGE(TAG, "Cannot register handler: MessageBus not initialized");
        return false;
    }

    bool primaryResult = false;
    bool secondaryResult = false;

    // Register with primary transport
    if (primaryTransport && primaryTransport->RegisterHandler) {
        primaryResult = primaryTransport->RegisterHandler(handler);
        ESP_LOGI(TAG, "Registered handler '%s' with primary transport: %s",
                 handler.Identifier.c_str(), primaryResult ? "success" : "failed");
    }

    // Register with secondary transport if in dual mode
    if (dualTransportMode && secondaryTransport && secondaryTransport->RegisterHandler) {
        secondaryResult = secondaryTransport->RegisterHandler(handler);
        ESP_LOGI(TAG, "Registered handler '%s' with secondary transport: %s",
                 handler.Identifier.c_str(), secondaryResult ? "success" : "failed");
    }

    return primaryResult || secondaryResult;
}

bool MessageBus::UnregisterHandler(const String& identifier) {
    if (!initialized) {
        return false;
    }

    bool primaryResult = false;
    bool secondaryResult = false;

    // Unregister from primary transport
    if (primaryTransport && primaryTransport->UnregisterHandler) {
        primaryResult = primaryTransport->UnregisterHandler(identifier);
    }

    // Unregister from secondary transport if in dual mode
    if (dualTransportMode && secondaryTransport && secondaryTransport->UnregisterHandler) {
        secondaryResult = secondaryTransport->UnregisterHandler(identifier);
    }

    ESP_LOGI(TAG, "Unregistered handler '%s'", identifier.c_str());
    return primaryResult || secondaryResult;
}

void MessageBus::SetTransport(Transport* transport) {
    if (!transport) {
        ESP_LOGE(TAG, "Cannot set null transport");
        return;
    }

    ESP_LOGI(TAG, "Setting primary transport");
    primaryTransport = transport;
    dualTransportMode = false;

    // Initialize the transport
    if (transport->Init) {
        transport->Init();
    }
}

void MessageBus::EnableMqttTransport() {
    ESP_LOGI(TAG, "Enabling MQTT transport");
    SetTransport(Transports::GetMqttTransport());
}

void MessageBus::EnableSerialTransport() {
    ESP_LOGI(TAG, "Enabling Serial transport");
    SetTransport(Transports::GetSerialTransport());
}

void MessageBus::EnableBothTransports() {
    ESP_LOGI(TAG, "Enabling dual transport mode (MQTT + Serial)");

    primaryTransport = Transports::GetMqttTransport();
    secondaryTransport = Transports::GetSerialTransport();
    dualTransportMode = true;

    // Initialize both transports
    if (primaryTransport && primaryTransport->Init) {
        primaryTransport->Init();
    }

    if (secondaryTransport && secondaryTransport->Init) {
        secondaryTransport->Init();
    }
}

unsigned long MessageBus::GetLastActivity() {
    return lastActivity;
}

// Private helper methods
bool MessageBus::PublishToTransport(Transport* transport, const char* topic, const char* payload) {
    if (!transport || !transport->Publish) {
        return false;
    }

    bool result = transport->Publish(topic, payload);
    if (result) {
        ESP_LOGI(TAG, "Published to topic '%s': %s", topic, payload);
    } else {
        ESP_LOGW(TAG, "Failed to publish to topic '%s'", topic);
    }

    return result;
}

void MessageBus::UpdateActivity() {
    lastActivity = millis();
}

// Typed convenience method implementations
bool MessageBus::PublishAudioStatusRequest(const Messages::AudioStatusRequest& request) {
    return PublishTyped("STATUS_REQUEST", request);
}

bool MessageBus::PublishAudioStatusResponse(const Messages::AudioStatusResponse& response) {
    return PublishTyped("STATUS_UPDATE", response);
}

bool MessageBus::RegisterAudioStatusHandler(const String& identifier, TypedMessageCallback<Messages::AudioStatusResponse> callback) {
    return RegisterTypedHandler<Messages::AudioStatusResponse>("STATUS", identifier, callback);
}

}  // namespace Messaging
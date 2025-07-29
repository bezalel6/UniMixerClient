# Server Requirements for UniMixer Serial Communication

This document defines the platform-agnostic server requirements for communicating with the UniMixer ESP32 client via serial connection.

## Overview

The UniMixer client expects a serial server that implements a binary-framed JSON messaging protocol. The server must handle bidirectional communication with the ESP32 device over a serial connection (USB/UART).

## Serial Configuration

- **Baud Rate**: 115200
- **Data Bits**: 8
- **Stop Bits**: 1
- **Parity**: None
- **Flow Control**: None
- **Read/Write Timeout**: 1000ms

## Binary Protocol Specification

### Frame Format

Messages are transmitted using a binary framing protocol with the following structure:

```
[START][LENGTH][CRC16][TYPE][PAYLOAD][END]
```

- **START**: `0x7E` (1 byte) - Frame start marker
- **LENGTH**: 4 bytes (little-endian) - Length of the unescaped payload
- **CRC16**: 2 bytes (little-endian) - CRC16-MODBUS of the unescaped payload
- **TYPE**: 1 byte - Message type (currently only `0x01` for JSON)
- **PAYLOAD**: Variable length - Escaped JSON payload
- **END**: `0x7F` (1 byte) - Frame end marker

### Escape Sequences

The following bytes must be escaped within the payload:
- `0x7E` (START_MARKER) → `0x7D 0x5E`
- `0x7F` (END_MARKER) → `0x7D 0x5F`
- `0x7D` (ESCAPE_CHAR) → `0x7D 0x5D`

Escape mechanism: `ESCAPE_CHAR (0x7D)` followed by `original_byte XOR 0x20`

### CRC16 Calculation

- **Algorithm**: CRC16-MODBUS
- **Polynomial**: 0xA001
- **Initial Value**: 0xFFFF
- **Input**: Unescaped payload bytes only
- **Implementation**:
```c
uint16_t crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
```

## Message Protocol

### Message Structure

All messages are JSON objects with the following base fields:

```json
{
    "messageType": "string",     // Required: Message type identifier
    "deviceId": "string",        // Required: Source device identifier
    "requestId": "string",       // Required: Unique request identifier
    "timestamp": number          // Required: Unix timestamp (milliseconds)
}
```

### Message Types

#### 1. GET_STATUS (Client → Server)
Request current audio status from the server.

```json
{
    "messageType": "GET_STATUS",
    "deviceId": "ESP32_DEVICE_ID",
    "requestId": "unique-request-id",
    "timestamp": 1234567890
}
```

#### 2. AUDIO_STATUS (Server → Client)
Audio status response containing session and device information.

```json
{
    "messageType": "STATUS_MESSAGE",
    "deviceId": "SERVER_ID",
    "requestId": "response-id",
    "timestamp": 1234567890,
    "activeSessionCount": 2,
    "sessions": [
        {
            "processId": 12345,
            "processName": "chrome",
            "displayName": "Google Chrome",
            "volume": 0.75,
            "isMuted": false,
            "state": "AudioSessionStateActive"
        }
    ],
    "defaultDevice": {
        "friendlyName": "Speakers (Realtek Audio)",
        "volume": 0.5,
        "isMuted": false,
        "dataFlow": "Render",
        "deviceRole": "Console"
    },
    "reason": "SessionChange",
    "originatingRequestId": "original-request-id",
    "originatingDeviceId": "ESP32_DEVICE_ID"
}
```

**Note**: The client expects `messageType: "STATUS_MESSAGE"` which maps internally to `AUDIO_STATUS`.

#### 3. SET_VOLUME (Client → Server)
Set volume for a specific process or default device.

```json
{
    "messageType": "SET_VOLUME",
    "deviceId": "ESP32_DEVICE_ID",
    "requestId": "unique-request-id",
    "timestamp": 1234567890,
    "processName": "chrome",
    "volume": 75,
    "target": "default"  // or specific device name
}
```

#### 4. ASSET_REQUEST (Client → Server)
Request icon/asset for a specific process.

```json
{
    "messageType": "GET_ASSETS",
    "deviceId": "ESP32_DEVICE_ID",
    "requestId": "unique-request-id",
    "timestamp": 1234567890,
    "processName": "chrome"
}
```

#### 5. ASSET_RESPONSE (Server → Client)
Response containing base64-encoded asset data.

```json
{
    "messageType": "ASSET_RESPONSE",
    "deviceId": "SERVER_ID",
    "requestId": "response-id",
    "timestamp": 1234567890,
    "processName": "chrome",
    "success": true,
    "assetData": "base64-encoded-image-data",
    "width": 64,
    "height": 64,
    "format": "PNG",
    "errorMessage": ""
}
```

### Message Type Mappings

The client uses internal message type strings that differ from the JSON field values:

| Internal Type | JSON messageType Field |
|--------------|------------------------|
| AUDIO_STATUS | STATUS_MESSAGE |
| VOLUME_CHANGE | VOLUME_CHANGE |
| MUTE_TOGGLE | MUTE_TOGGLE |
| ASSET_REQUEST | GET_ASSETS |
| ASSET_RESPONSE | ASSET_RESPONSE |
| GET_STATUS | GET_STATUS |
| SET_VOLUME | SET_VOLUME |
| SET_DEFAULT_DEVICE | SET_DEFAULT_DEVICE |

## Server Behavioral Requirements

### 1. Connection Management
- Support multiple simultaneous client connections
- Handle connection/disconnection gracefully
- Implement keep-alive mechanism (respond to GET_STATUS as heartbeat)

### 2. Message Processing
- Parse incoming binary frames and validate CRC16
- Deserialize JSON payloads
- Route messages based on messageType
- Generate appropriate responses with matching requestId

### 3. Audio Session Monitoring
- Monitor system audio sessions in real-time
- Track process names, volumes, and mute states
- Monitor default audio device changes
- Send unsolicited AUDIO_STATUS updates on changes

### 4. Asset Management
- Extract process icons (64x64 recommended)
- Convert icons to PNG format
- Base64 encode image data for transmission
- Cache frequently requested assets

### 5. Error Handling
- Return error responses for invalid requests
- Include meaningful error messages in responses
- Log protocol violations for debugging
- Implement frame timeout (1000ms)

### 6. Performance Requirements
- Process messages within 100ms
- Batch updates to prevent flooding (max 10 updates/second)
- Limit message payload size to 8KB
- Implement message queuing for reliability

## Implementation Examples

### Frame Encoding (Python)
```python
def encode_frame(json_payload):
    payload_bytes = json_payload.encode('utf-8')
    length = len(payload_bytes)
    crc = calculate_crc16(payload_bytes)
    
    frame = bytearray()
    frame.append(0x7E)  # START
    frame.extend(length.to_bytes(4, 'little'))  # LENGTH
    frame.extend(crc.to_bytes(2, 'little'))     # CRC16
    frame.append(0x01)  # TYPE (JSON)
    
    # Apply escape sequences to payload
    for byte in payload_bytes:
        if byte in [0x7E, 0x7F, 0x7D]:
            frame.append(0x7D)
            frame.append(byte ^ 0x20)
        else:
            frame.append(byte)
    
    frame.append(0x7F)  # END
    return bytes(frame)
```

### Frame Decoding State Machine
```python
class FrameDecoder:
    def __init__(self):
        self.state = 'WAIT_START'
        self.header = bytearray()
        self.payload = bytearray()
        self.expected_length = 0
        self.expected_crc = 0
        self.escape_next = False
    
    def process_byte(self, byte):
        if self.state == 'WAIT_START':
            if byte == 0x7E:
                self.state = 'READ_HEADER'
                self.reset_buffers()
        
        elif self.state == 'READ_HEADER':
            self.header.append(byte)
            if len(self.header) == 7:  # LENGTH(4) + CRC(2) + TYPE(1)
                self.parse_header()
                self.state = 'READ_PAYLOAD'
        
        elif self.state == 'READ_PAYLOAD':
            if byte == 0x7F and not self.escape_next:
                return self.validate_message()
            else:
                self.process_payload_byte(byte)
```

## Testing & Validation

### Test Messages
1. **Connection Test**: Send GET_STATUS, expect AUDIO_STATUS response
2. **Volume Control**: Send SET_VOLUME, verify volume change
3. **Asset Request**: Send ASSET_REQUEST, verify base64 response
4. **Error Handling**: Send malformed messages, verify error responses
5. **Performance**: Send rapid requests, verify <100ms response time

### Protocol Compliance
- Verify correct frame structure
- Test escape sequence handling
- Validate CRC16 calculation
- Ensure JSON field compliance
- Test maximum payload size limits

## Security Considerations

1. **Input Validation**: Validate all incoming JSON fields
2. **Size Limits**: Enforce maximum message size (8KB)
3. **Rate Limiting**: Implement per-client rate limits
4. **Authentication**: Consider adding device authentication tokens
5. **Encryption**: Consider TLS for network transports (future)

## Version Compatibility

- **Protocol Version**: 1.0
- **Minimum Client Version**: UniMixer 1.0
- **Backward Compatibility**: Not required for v1.0
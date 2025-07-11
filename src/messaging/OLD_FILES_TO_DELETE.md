# Old Messaging Files to Delete

## Once migration is complete, delete these files:

### Core Messaging Files
- `src/messaging/MessageAPI.h`
- `src/messaging/MessageAPI.cpp`
- `src/messaging/system/MessageCore.h`
- `src/messaging/system/MessageCore.cpp`

### Protocol Files
- `src/messaging/protocol/MessageData.h`
- `src/messaging/protocol/MessageData.cpp`
- `src/messaging/protocol/MessageTypes.h`
- `src/messaging/protocol/MessageTypes.cpp`
- `src/messaging/protocol/ExternalMessage.h`
- `src/messaging/protocol/MessageShapes.h`
- `src/messaging/protocol/MessageShapeDefinitions.h`
- `src/messaging/protocol/JsonToVariantConverter.h`

### Transport Files
- `src/messaging/transport/SerialEngine.h`
- `src/messaging/transport/SerialEngine.cpp`
- `src/messaging/transport/BinaryProtocol.h` (keep if needed for framing)

### Documentation
- `src/messaging/MESSAGING_QUICK_START.md` (old)
- `src/messaging/MESSAGING_SYSTEM_DESIGN.md` (old)

## Files to Keep

### New System
- `src/messaging/Message.h`
- `src/messaging/Message.cpp`
- `src/messaging/SimplifiedSerialEngine.h`
- `src/messaging/SimplifiedSerialEngine.cpp`
- `src/messaging/MessagingInit.cpp`

### Temporary (delete after full migration)
- `src/messaging/MessageMigrationAdapter.h`

### May Need to Keep
- `src/messaging/protocol/MessageConfig.h` (for device ID, etc)
- `src/messaging/protocol/BinaryProtocol.h` (for framing)

## Migration Status

- [x] Created new Message system
- [x] Created SimplifiedSerialEngine
- [x] Created migration adapter
- [x] Updated AudioManager to use adapter
- [x] Updated MessageBusLogoSupplier to use adapter
- [x] Updated AppController initialization
- [ ] Test the new system
- [ ] Remove migration adapter usage
- [ ] Delete old files
- [ ] Update build system (CMake/platformio)

    # LVGL Image Roller Component Design Specification

## Overview
A custom LVGL roller component designed for visual image selection, optimized for audio device selection in the UniMixer client. This component extends LVGL's standard roller with image display capabilities.

## Component Architecture

### Core Structure
```c
typedef struct {
    lv_obj_t* roller;           // Base LVGL roller object
    lv_obj_t* image_display;    // Image preview container
    lv_obj_t* current_image;    // Currently displayed image
    const char** image_paths;   // Array of image paths
    uint16_t image_count;       // Number of images
    uint16_t current_index;     // Currently selected index
    bool use_sd_card;          // Load from SD card vs embedded
} lv_image_roller_t;
```

### Component Layout
```
+--------------------------------+
|     Image Display Area         |
|   +----------------------+     |
|   |                      |     |
|   |   Selected Image     |     |
|   |    (128x128)         |     |
|   |                      |     |
|   +----------------------+     |
|                                |
|     Text Roller Area           |
|   +----------------------+     |
|   | Chrome               | ▲   |
|   | Discord              | █   |
|   | Spotify              | █   |
|   | System Sounds        | ▼   |
|   +----------------------+     |
+--------------------------------+
```

## API Design

### Factory Function
```c
lv_obj_t* lv_image_roller_create(lv_obj_t* parent);
```

### Configuration Functions
```c
// Set options with associated images
void lv_image_roller_set_options(lv_obj_t* roller,
                                const char* options,
                                const char** image_paths,
                                uint16_t count,
                                bool use_sd_card);

// Set selected index
void lv_image_roller_set_selected(lv_obj_t* roller, uint16_t idx, lv_anim_enable_t anim);

// Get selected index
uint16_t lv_image_roller_get_selected(const lv_obj_t* roller);

// Set image size
void lv_image_roller_set_image_size(lv_obj_t* roller, lv_coord_t width, lv_coord_t height);

// Set visible row count
void lv_image_roller_set_visible_row_count(lv_obj_t* roller, uint8_t row_cnt);
```

### Event Types
```c
// Custom event when selection changes
LV_EVENT_IMAGE_ROLLER_CHANGED
```

## Implementation Details

### Image Loading Strategy

#### Option 1: Embedded Images
```c
// Image descriptors array
static const lv_image_dsc_t* device_images[] = {
    &img_chrome_icon,
    &img_discord_icon,
    &img_spotify_icon,
    &img_system_icon
};
```

#### Option 2: SD Card Images
```c
// Image paths for SD card loading
static const char* device_image_paths[] = {
    "S:/icons/chrome.png",
    "S:/icons/discord.png",
    "S:/icons/spotify.png",
    "S:/icons/system.png"
};
```

### Memory Management
- **Image Caching**: Keep only current and adjacent images in memory
- **PSRAM Usage**: Store decoded images in PSRAM when available
- **Lazy Loading**: Load images on-demand as roller scrolls

### Event Handling
```c
static void image_roller_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* roller = lv_event_get_target(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        lv_image_roller_t* img_roller = (lv_image_roller_t*)lv_obj_get_user_data(roller);
        uint16_t selected = lv_roller_get_selected(img_roller->roller);

        // Update displayed image
        update_displayed_image(img_roller, selected);

        // Send custom event
        lv_obj_send_event(roller, LV_EVENT_IMAGE_ROLLER_CHANGED, NULL);
    }
}
```

## Styling and Themes

### Default Style Configuration
```c
static void apply_default_style(lv_obj_t* roller) {
    // Container style
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_radius(roller, 10, 0);
    lv_obj_set_style_pad_all(roller, 10, 0);

    // Image area style
    lv_obj_set_style_bg_color(image_area, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_radius(image_area, 8, 0);

    // Roller text style
    lv_obj_set_style_text_font(roller_obj, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller_obj, lv_color_hex(0xffffff), LV_PART_MAIN);
}
```

### Animation Support
```c
// Smooth image transitions
static void animate_image_change(lv_obj_t* old_img, lv_obj_t* new_img) {
    // Fade out old image
    lv_anim_t a_out;
    lv_anim_init(&a_out);
    lv_anim_set_var(&a_out, old_img);
    lv_anim_set_values(&a_out, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_exec_cb(&a_out, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_time(&a_out, 200);

    // Fade in new image
    lv_anim_t a_in;
    lv_anim_init(&a_in);
    lv_anim_set_var(&a_in, new_img);
    lv_anim_set_values(&a_in, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a_in, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_time(&a_in, 200);
    lv_anim_set_delay(&a_in, 100);

    lv_anim_start(&a_out);
    lv_anim_start(&a_in);
}
```

## Usage Examples

### Basic Usage
```c
// Create image roller
lv_obj_t* device_selector = lv_image_roller_create(parent);

// Set options and images
const char* devices = "Chrome\nDiscord\nSpotify\nSystem Sounds";
lv_image_roller_set_options(device_selector, devices, device_image_paths, 4, true);

// Configure appearance
lv_image_roller_set_image_size(device_selector, 128, 128);
lv_image_roller_set_visible_row_count(device_selector, 3);

// Add event handler
lv_obj_add_event_cb(device_selector, device_selected_cb, LV_EVENT_IMAGE_ROLLER_CHANGED, NULL);
```

### Event Handler Example
```c
static void device_selected_cb(lv_event_t* e) {
    lv_obj_t* roller = lv_event_get_target(e);
    uint16_t selected = lv_image_roller_get_selected(roller);

    char buf[32];
    lv_roller_get_selected_str(roller, buf, sizeof(buf));

    ESP_LOGI(TAG, "Selected device: %s (index: %d)", buf, selected);

    // Send message to audio system
    Message msg = Message::createAudioDeviceSelect(buf, deviceId);
    messagingService.sendMessage(msg);
}
```

## Performance Considerations

### Multi-Core Optimization
- **Core 0**: UI rendering and image display updates
- **Core 1**: Image loading operations (if using SD card)
- **Mutex Protection**: Thread-safe image buffer access

### Memory Budgets
- **Per Image**: ~50KB for 128x128 ARGB8888
- **Total Cache**: 3 images max (current + adjacent)
- **PSRAM Allocation**: Prefer PSRAM for image buffers

### Rendering Optimization
- **Dirty Region**: Only invalidate changed areas
- **Double Buffering**: Smooth transitions without flicker
- **Frame Limiting**: Cap updates to 30 FPS for roller

## Integration Points

### With Audio System
```c
// Message factory for device selection
Message Message::createAudioDeviceSelect(const char* device_name, uint8_t device_id) {
    Message msg;
    msg.type = MessageType::AUDIO_DEVICE_SELECT;
    msg.data.audio.device_name = device_name;
    msg.data.audio.device_id = device_id;
    return msg;
}
```

### With SquareLine Studio
- Export as custom widget for visual design
- Maintain property editor compatibility
- Support animation timeline integration

## Testing Checklist

- [ ] Smooth scrolling performance (30+ FPS)
- [ ] Image loading without UI blocking
- [ ] Memory usage within limits (<200KB total)
- [ ] Touch responsiveness (<100ms)
- [ ] SD card image loading reliability
- [ ] Animation smoothness
- [ ] Event propagation correctness
- [ ] Multi-language text support
- [ ] Different image sizes handling
- [ ] Error handling (missing images)

## Future Enhancements

1. **Circular Roller Mode**: Infinite scrolling
2. **Multi-Column Support**: Grid of images
3. **Preview Zoom**: Tap to enlarge image
4. **Lazy Loading**: Progressive image quality
5. **Icon Badges**: Overlay indicators
6. **Swipe Gestures**: Quick navigation
7. **Search Filter**: Text input filtering
8. **Thumbnail Generation**: Auto-resize large images

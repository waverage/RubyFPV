#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cairo/cairo.h>
#include <cstring>

struct Buffer {
    uint32_t handle;
    uint32_t pitch;
    uint32_t size;
    uint32_t fb_id;
    uint8_t *map;
};

int main() {
    // 1. Open the DRM device (try card1 if card0 fails to find connectors)
    int fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("Error: Cannot open /dev/dri/card1");
        return 1;
    }

    // 2. Get Resources
    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        perror("Error: drmModeGetResources failed");
        close(fd);
        return 1;
    }

    // 3. Find a connected HDMI output
    drmModeConnector *conn = nullptr;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            std::cout << "Found connected monitor on connector " << conn->connector_id << std::endl;
            break;
        }
        drmModeFreeConnector(conn);
        conn = nullptr;
    }

    if (!conn) {
        std::cerr << "Error: No active HDMI monitor detected!" << std::endl;
        drmModeFreeResources(res);
        close(fd);
        return 1;
    }

    // 4. Find an Encoder (the bridge between CRTC and Connector)
    drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
    if (!enc) {
        std::cerr << "Error: Could not get encoder" << std::endl;
        return 1;
    }
    uint32_t crtc_id = enc->crtc_id;

    // 5. Select video mode (resolution)
    drmModeModeInfo mode = conn->modes[0];
    std::cout << "Resolution: " << mode.hdisplay << "x" << mode.vdisplay << std::endl;

    // 6. Create Dumb Buffer
    struct drm_mode_create_dumb creq = {};
    creq.width = mode.hdisplay;
    creq.height = mode.vdisplay;
    creq.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("Error: Create Dumb Buffer failed");
        return 1;
    }

    Buffer buf = { .handle = creq.handle, .pitch = creq.pitch, .size = creq.size };

    // 7. Add Framebuffer
    if (drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32, buf.pitch, buf.handle, &buf.fb_id)) {
        perror("Error: Add Framebuffer failed");
        return 1;
    }

    // 8. Map buffer to memory
    struct drm_mode_map_dumb mreq = { .handle = buf.handle };
    drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    buf.map = (uint8_t *)mmap(0, buf.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);

    if (buf.map == MAP_FAILED) {
        perror("Error: mmap failed");
        return 1;
    }

    // 9. Draw with Cairo
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        buf.map, CAIRO_FORMAT_ARGB32, mode.hdisplay, mode.vdisplay, buf.pitch);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1); // Dark Gray background
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0, 1, 0); // Green Rectangle
    cairo_rectangle(cr, 200, 200, 400, 300);
    cairo_fill(cr);

    // 10. Display the buffer
    if (drmModeSetCrtc(fd, crtc_id, buf.fb_id, 0, 0, &conn->connector_id, 1, &mode)) {
        perror("Error: drmModeSetCrtc failed");
        return 1;
    }

    std::cout << "Success! Displaying for 5 seconds..." << std::endl;
    sleep(5);

    // Cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    munmap(buf.map, buf.size);
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    close(fd);

    return 0;
}

// How to build:
// g++ drm_cairo.cpp -o drm_cairo $(pkg-config --cflags --libs libdrm cairo)